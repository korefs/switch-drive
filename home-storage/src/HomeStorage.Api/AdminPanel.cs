using System.Net;
using System.Security.Claims;
using System.Security.Cryptography;
using System.Text;
using Microsoft.AspNetCore.Antiforgery;
using Microsoft.AspNetCore.Authentication;
using Microsoft.AspNetCore.Authentication.Cookies;
using Microsoft.EntityFrameworkCore;

namespace HomeStorage;

public static class AdminPanel
{
    public static void Map(WebApplication app, RuntimeOptions runtime)
    {
        app.MapGet("/", async (HomeStorageDb db) => await db.Admins.AnyAsync() ? Results.Redirect("/admin") : Results.Redirect("/setup"));
        app.MapGet("/setup", async (HttpContext ctx, HomeStorageDb db, IAntiforgery anti) =>
        {
            if (await db.Admins.AnyAsync()) return Results.Redirect("/login");
            var settings = await db.Settings.AsNoTracking().SingleAsync();
            return Page("Initial setup", SetupForm(anti.GetAndStoreTokens(ctx).RequestToken!, settings));
        });
        app.MapPost("/setup", async (HttpContext ctx, HomeStorageDb db, AuthService auth, LibraryPathPolicy paths, IAntiforgery anti) =>
        {
            if (await db.Admins.AnyAsync()) return Results.NotFound();
            try { await anti.ValidateRequestAsync(ctx); } catch { return Results.BadRequest("Invalid anti-forgery token."); }
            var f = await ctx.Request.ReadFormAsync();
            if (!SecretEquals(runtime.SetupToken, f["setupToken"].ToString())) return Results.BadRequest("Invalid setup token.");
            var admin = f["admin"].ToString(); var adminPassword = f["adminPassword"].ToString(); var user = f["libraryUser"].ToString(); var password = f["libraryPassword"].ToString(); var library = f["libraryPath"].ToString();
            if (admin.Length < 3 || adminPassword.Length < 12 || user.Length < 3 || password.Length < 12) return Results.BadRequest("Usernames need 3 characters and passwords need 12 characters.");
            if (!paths.TryValidateLibraryPath(library, out var canonical, out var error)) return Results.BadRequest(error);
            db.Admins.Add(new() { Username = admin, PasswordHash = auth.HashPassword(admin, adminPassword) });
            db.LibraryCredentials.Add(new() { Username = user, PasswordHash = auth.HashPassword(user, password), AllowCatalogManage = f["allowManage"] == "on" });
            var settings = await db.Settings.SingleAsync(); settings.InstanceName = Clean(f["instanceName"], "Home Storage"); settings.LibraryPath = canonical; settings.AuthRequired = f["anonymous"] != "on";
            await db.SaveChangesAsync(); return Results.Redirect("/login");
        }).RequireRateLimiting("auth");

        app.MapGet("/login", async (HttpContext ctx, IAntiforgery anti) => Page("Login", $"<form method=post>{Token(anti.GetAndStoreTokens(ctx).RequestToken!)}<label>User<input name=user autocomplete=username></label><label>Password<input type=password name=password autocomplete=current-password></label><button>Login</button></form>"));
        app.MapPost("/login", async (HttpContext ctx, HomeStorageDb db, AuthService auth, IAntiforgery anti, ILogger<Program> logger) =>
        {
            try { await anti.ValidateRequestAsync(ctx); } catch { return Results.BadRequest(); }
            var f = await ctx.Request.ReadFormAsync(); var user = f["user"].ToString(); var admin = await db.Admins.SingleOrDefaultAsync();
            if (admin is null || admin.Username != user || !auth.VerifyPassword(user, admin.PasswordHash, f["password"].ToString())) { logger.LogWarning("Administrative login failed from {RemoteIp}", ctx.Connection.RemoteIpAddress); return Results.Unauthorized(); }
            await ctx.SignInAsync(CookieAuthenticationDefaults.AuthenticationScheme, new ClaimsPrincipal(new ClaimsIdentity(new[] { new Claim(ClaimTypes.Name, user) }, CookieAuthenticationDefaults.AuthenticationScheme)));
            return Results.Redirect("/admin");
        }).RequireRateLimiting("auth");
        app.MapPost("/logout", async (HttpContext ctx, IAntiforgery anti) => { await anti.ValidateRequestAsync(ctx); await ctx.SignOutAsync(); return Results.Redirect("/login"); }).RequireAuthorization();

        app.MapGet("/admin", async (HttpContext ctx, HomeStorageDb db, IAntiforgery anti) =>
        {
            var settings = await db.Settings.AsNoTracking().SingleAsync();
            var files = await db.Catalog.CountAsync(x => x.Kind == CatalogKind.File && x.Active && !x.Suppressed); var hidden = await db.Catalog.CountAsync(x => x.Suppressed); var tokenCount = await db.DeviceTokens.CountAsync(x => x.RevokedAt == null);
            var catalog = await db.Catalog.AsNoTracking().OrderBy(x => x.RelativePath).Take(500).ToListAsync(); var devices = await db.DeviceTokens.AsNoTracking().OrderByDescending(x => x.CreatedAt).Take(100).ToListAsync(); var csrf = anti.GetAndStoreTokens(ctx).RequestToken!;
            var body = $"<p>Status: <b>{H(settings.LastScanStatus)}</b> &middot; Files: {files} &middot; Hidden: {hidden} &middot; Devices: {tokenCount}</p><p>Library: <code>{H(settings.LibraryPath)}</code></p>" +
                $"<form method=post action=/admin/scan>{Token(csrf)}<button>Scan now</button></form>" +
                $"<form method=post action=/admin/settings>{Token(csrf)}<label>Name<input name=name value=\"{H(settings.InstanceName)}\"></label><label>Container library path<input name=path value=\"{H(settings.LibraryPath)}\"></label><label class=inline><input type=checkbox name=anonymous {(settings.AuthRequired ? "" : "checked")}> Anonymous read access</label><button>Save settings</button></form>" +
                $"<h2>Rotate Switch credentials</h2><form method=post action=/admin/library-credentials>{Token(csrf)}<label>Username<input name=user autocomplete=username></label><label>New password (12+)<input type=password name=password autocomplete=new-password></label><label class=inline><input type=checkbox name=allowManage> Allow Switch to hide entries</label><button>Rotate and revoke devices</button></form>" +
                $"<h2>Catalog</h2><p>Showing at most 500 entries. Hiding changes only SQLite; the physical file is never deleted.</p>{CatalogRows(catalog, csrf)}<h2>Device tokens</h2>{TokenRows(devices, csrf)}" +
                $"<p><a href=/api/admin/catalog>Catalog JSON</a> &middot; <a href=/api/admin/tokens>Device tokens JSON</a></p><form method=post action=/logout>{Token(csrf)}<button>Log out</button></form>";
            return Page("Home Storage", body);
        }).RequireAuthorization();

        app.MapPost("/admin/scan", async (HttpContext ctx, IAntiforgery anti, ScanTrigger trigger) => { await anti.ValidateRequestAsync(ctx); trigger.Request(); return Results.Redirect("/admin"); }).RequireAuthorization();
        app.MapPost("/admin/settings", async (HttpContext ctx, HomeStorageDb db, LibraryPathPolicy paths, IAntiforgery anti, ScanTrigger trigger) => { await anti.ValidateRequestAsync(ctx); var f = await ctx.Request.ReadFormAsync(); if (!paths.TryValidateLibraryPath(f["path"].ToString(), out var path, out var error)) return Results.BadRequest(error); var s = await db.Settings.SingleAsync(); s.InstanceName = Clean(f["name"], "Home Storage"); s.LibraryPath = path; s.AuthRequired = f["anonymous"] != "on"; await db.SaveChangesAsync(); trigger.Request(); return Results.Redirect("/admin"); }).RequireAuthorization();
        app.MapPost("/admin/library-credentials", async (HttpContext ctx, HomeStorageDb db, AuthService auth, IAntiforgery anti) => { await anti.ValidateRequestAsync(ctx); var f = await ctx.Request.ReadFormAsync(); var user = f["user"].ToString(); var password = f["password"].ToString(); if (user.Length < 3 || password.Length < 12) return Results.BadRequest("Username needs 3 characters and password needs 12 characters."); var credential = await db.LibraryCredentials.SingleAsync(); credential.Username = user; credential.PasswordHash = auth.HashPassword(user, password); credential.AllowCatalogManage = f["allowManage"] == "on"; credential.Version++; foreach (var token in await db.DeviceTokens.Where(x => x.RevokedAt == null).ToListAsync()) token.RevokedAt = DateTimeOffset.UtcNow; await db.SaveChangesAsync(); return Results.Redirect("/admin"); }).RequireAuthorization();
        app.MapPost("/admin/catalog/{id}/hide", async (HttpContext ctx, string id, HomeStorageDb db, IAntiforgery anti) => { await anti.ValidateRequestAsync(ctx); var e = await db.Catalog.SingleOrDefaultAsync(x => x.Id == id); if (e is null) return Results.NotFound(); await SetSuppressed(db, e, true); return Results.Redirect("/admin"); }).RequireAuthorization();
        app.MapPost("/admin/catalog/{id}/restore", async (HttpContext ctx, string id, HomeStorageDb db, IAntiforgery anti) => { await anti.ValidateRequestAsync(ctx); var e = await db.Catalog.SingleOrDefaultAsync(x => x.Id == id); if (e is null) return Results.NotFound(); await SetSuppressed(db, e, false); return Results.Redirect("/admin"); }).RequireAuthorization();
        app.MapPost("/admin/tokens/{id}/revoke", async (HttpContext ctx, string id, HomeStorageDb db, IAntiforgery anti) => { await anti.ValidateRequestAsync(ctx); var token = await db.DeviceTokens.SingleOrDefaultAsync(x => x.Id == id); if (token is null) return Results.NotFound(); token.RevokedAt ??= DateTimeOffset.UtcNow; await db.SaveChangesAsync(); return Results.Redirect("/admin"); }).RequireAuthorization();

        app.MapGet("/api/admin/status", async (HomeStorageDb db) => Results.Json(await db.Settings.AsNoTracking().SingleAsync())).RequireAuthorization();
        app.MapGet("/api/admin/catalog", async (HomeStorageDb db) => Results.Json(await db.Catalog.AsNoTracking().OrderBy(x => x.RelativePath).Select(x => new { x.Id, x.Kind, x.RelativePath, x.Size, x.Active, x.Suppressed, x.ETag }).ToListAsync())).RequireAuthorization();
        app.MapGet("/api/admin/tokens", async (HomeStorageDb db) => Results.Json(await db.DeviceTokens.AsNoTracking().Select(x => new { x.Id, x.Name, x.CanManageCatalog, x.CreatedAt, x.LastUsedAt, x.RevokedAt }).ToListAsync())).RequireAuthorization();
        app.MapPost("/api/admin/catalog/{id}/restore", async (HttpContext ctx, string id, HomeStorageDb db, IAntiforgery anti) => { await anti.ValidateRequestAsync(ctx); var e = await db.Catalog.SingleOrDefaultAsync(x => x.Id == id); if (e is null) return Results.NotFound(); await SetSuppressed(db, e, false); return Results.NoContent(); }).RequireAuthorization();
        app.MapDelete("/api/admin/tokens/{id}", async (HttpContext ctx, string id, HomeStorageDb db, IAntiforgery anti) => { await anti.ValidateRequestAsync(ctx); var token = await db.DeviceTokens.SingleOrDefaultAsync(x => x.Id == id); if (token is null) return Results.NotFound(); token.RevokedAt = DateTimeOffset.UtcNow; await db.SaveChangesAsync(); return Results.NoContent(); }).RequireAuthorization();
    }

    private static string SetupForm(string token, ServiceSettings settings) => $"<form method=post>{Token(token)}<label>Setup token<input type=password name=setupToken></label><label>Admin user<input name=admin autocomplete=username></label><label>Admin password (12+)<input type=password name=adminPassword autocomplete=new-password></label><label>Instance name<input name=instanceName value=\"{H(settings.InstanceName)}\"></label><label>Container library path<input name=libraryPath value=\"{H(settings.LibraryPath)}\"></label><label>Switch user<input name=libraryUser></label><label>Switch password (12+)<input type=password name=libraryPassword></label><label class=inline><input type=checkbox name=allowManage> Allow Switch to hide entries</label><label class=inline><input type=checkbox name=anonymous> Anonymous read access</label><button>Initialize</button></form>";
    private static string CatalogRows(IEnumerable<CatalogEntry> entries, string csrf) => "<div class=scroll><table><thead><tr><th>Path</th><th>State</th><th>Size</th><th>Action</th></tr></thead><tbody>" + string.Concat(entries.Select(e => $"<tr><td>{H(e.RelativePath)}</td><td>{(e.Suppressed ? "hidden" : e.Active ? "active" : "inactive")}</td><td>{e.Size}</td><td><form method=post action=/admin/catalog/{H(e.Id)}/{(e.Suppressed ? "restore" : "hide")}>{Token(csrf)}<button>{(e.Suppressed ? "Restore" : "Hide")}</button></form></td></tr>")) + "</tbody></table></div>";
    private static string TokenRows(IEnumerable<DeviceToken> entries, string csrf) => "<div class=scroll><table><thead><tr><th>Device</th><th>Created</th><th>Last used</th><th>State</th></tr></thead><tbody>" + string.Concat(entries.Select(e => $"<tr><td>{H(e.Name)}</td><td>{H(e.CreatedAt)}</td><td>{H(e.LastUsedAt)}</td><td>{(e.RevokedAt is null ? $"<form method=post action=/admin/tokens/{H(e.Id)}/revoke>{Token(csrf)}<button>Revoke</button></form>" : "revoked")}</td></tr>")) + "</tbody></table></div>";
    private static IResult Page(string title, string body) => Results.Content($"<!doctype html><html lang=en><meta charset=utf-8><meta name=viewport content='width=device-width'><title>{H(title)}</title><style>body{{font:16px system-ui;max-width:980px;margin:40px auto;padding:0 20px;background:#07172b;color:#eef}}label{{display:block;margin:14px 0}}label input{{display:block;width:100%;box-sizing:border-box;padding:9px}}label.inline input{{display:inline;width:auto}}button{{padding:8px 14px;margin:4px 0}}a{{color:#67dff5}}code{{overflow-wrap:anywhere}}.scroll{{overflow-x:auto}}table{{width:100%;border-collapse:collapse}}th,td{{padding:7px;border-bottom:1px solid #29405e;text-align:left}}td form{{margin:0}}</style><h1>{H(title)}</h1>{body}</html>", "text/html; charset=utf-8");
    private static string Token(string value) => $"<input type=hidden name=__RequestVerificationToken value=\"{H(value)}\">";
    private static string H(object? value) => WebUtility.HtmlEncode(value?.ToString() ?? "");
    private static string Clean(object value, string fallback) { var text = value.ToString()?.Trim(); return string.IsNullOrEmpty(text) ? fallback : text[..Math.Min(text.Length, 100)]; }
    private static bool SecretEquals(string expected, string actual) { if (string.IsNullOrEmpty(expected)) return false; var a = Encoding.UTF8.GetBytes(expected); var b = Encoding.UTF8.GetBytes(actual); return a.Length == b.Length && CryptographicOperations.FixedTimeEquals(a, b); }
    private static async Task SetSuppressed(HomeStorageDb db, CatalogEntry entry, bool suppressed) { if (entry.Kind == CatalogKind.Folder) { var prefix = entry.RelativePath + "/"; foreach (var child in await db.Catalog.Where(x => x.RelativePath == entry.RelativePath || x.RelativePath.StartsWith(prefix)).ToListAsync()) child.Suppressed = suppressed; } else entry.Suppressed = suppressed; await db.SaveChangesAsync(); }
}
