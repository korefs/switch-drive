using System.Net;
using System.Security.Claims;
using System.Security.Cryptography;
using System.Text;
using Microsoft.AspNetCore.Antiforgery;
using Microsoft.AspNetCore.Authentication;
using Microsoft.AspNetCore.Authentication.Cookies;
using Microsoft.AspNetCore.Identity;
using Microsoft.AspNetCore.RateLimiting;
using Microsoft.AspNetCore.DataProtection;
using Microsoft.AspNetCore.WebUtilities;
using Microsoft.EntityFrameworkCore;
using Microsoft.Net.Http.Headers;

using HomeStorage;

var builder = WebApplication.CreateBuilder(args);
var runtime = RuntimeOptions.FromConfiguration(builder.Configuration);
Directory.CreateDirectory(runtime.DataPath);
builder.WebHost.UseUrls($"http://0.0.0.0:{runtime.HttpPort}");
builder.Services.AddSingleton(runtime); builder.Services.AddSingleton<LibraryPathPolicy>(); builder.Services.AddSingleton<ScanState>(); builder.Services.AddSingleton<ScanTrigger>(); builder.Services.AddSingleton<DownloadGate>(); builder.Services.AddSingleton<MetadataPipeline>(); builder.Services.AddSingleton<CatalogIndexer>();
builder.Services.AddDbContextFactory<HomeStorageDb>(o => o.UseSqlite($"Data Source={Path.Combine(runtime.DataPath, "home-storage.db")}"));
builder.Services.AddSingleton<IPasswordHasher<string>, PasswordHasher<string>>(); builder.Services.AddSingleton<AuthService>();
if (!builder.Configuration.GetValue<bool>("DISABLE_BACKGROUND_SERVICES")) { builder.Services.AddHostedService<CatalogIndexerService>(); builder.Services.AddHostedService<DiscoveryService>(); }
builder.Services.AddDataProtection().PersistKeysToFileSystem(new DirectoryInfo(Path.Combine(runtime.DataPath, "keys")));
builder.Services.AddAntiforgery(o => o.HeaderName = "X-CSRF-TOKEN");
builder.Services.AddAuthentication(CookieAuthenticationDefaults.AuthenticationScheme).AddCookie(o => { o.LoginPath = "/login"; o.Cookie.HttpOnly = true; o.Cookie.SameSite = Microsoft.AspNetCore.Http.SameSiteMode.Strict; o.Cookie.SecurePolicy = CookieSecurePolicy.SameAsRequest; o.ExpireTimeSpan = TimeSpan.FromHours(8); });
builder.Services.AddAuthorization();
builder.Services.AddRateLimiter(o => { o.AddFixedWindowLimiter("auth", x => { x.PermitLimit = 10; x.Window = TimeSpan.FromMinutes(1); x.QueueLimit = 0; }); o.RejectionStatusCode = StatusCodes.Status429TooManyRequests; });
var app = builder.Build();
app.Use(async (ctx, next) =>
{
    if (ctx.Request.Path.StartsWithSegments("/api") || ctx.Request.Path.StartsWithSegments("/drive-health"))
        ctx.Response.OnStarting(() => { ctx.Response.Headers.CacheControl = "private, no-store"; return Task.CompletedTask; });
    await next();
});
app.UseRateLimiter(); app.UseAuthentication(); app.UseAuthorization();

await using (var scope = app.Services.CreateAsyncScope())
{
    var db = scope.ServiceProvider.GetRequiredService<HomeStorageDb>(); await db.Database.MigrateAsync();
    await db.Database.ExecuteSqlRawAsync("PRAGMA journal_mode=WAL;");
    if (!await db.Settings.AnyAsync()) { db.Settings.Add(new() { InstanceName = runtime.InitialInstanceName, LibraryPath = runtime.InitialLibraryPath }); await db.SaveChangesAsync(); }
}
app.Logger.LogInformation("Home Storage starting on port {Port}; library mount root {Root}", runtime.HttpPort, runtime.LibraryMountRoot);

app.MapGet("/health", async (HomeStorageDb db, ScanState scan) => { var s = await db.Settings.AsNoTracking().SingleAsync(); return Results.Json(new { status = s.LastScanStatus == "degraded" ? "degraded" : scan.IsScanning ? "scanning" : "ready", database = "ready", lastScanAt = s.LastScanAt }); });
app.MapGet("/drive-health", async (HomeStorageDb db) => { var s = await db.Settings.AsNoTracking().SingleAsync(); return Results.Json(new { service = "switch-drive-home-storage", protocolVersion = 1, instanceId = s.InstanceId, name = s.InstanceName, httpPort = runtime.HttpPort, authRequired = s.AuthRequired }); });

app.MapPost("/api/v1/auth/token", async (HttpContext ctx, AuthService auth, ILogger<Program> log) =>
{
    if (!AuthService.TryReadBasic(ctx.Request, out var username, out var password)) { log.LogWarning("Authorization failed from {RemoteIp}", ctx.Connection.RemoteIpAddress); return Results.Unauthorized(); }
    var (token, canManage, error) = await auth.ExchangeAsync(username, password, ctx.RequestAborted); if (token is null) { log.LogWarning("Authorization failed from {RemoteIp}: {Reason}", ctx.Connection.RemoteIpAddress, error); return Results.Unauthorized(); }
    var scopes = canManage ? new[] { "catalog:read", "files:read", "catalog:manage" } : new[] { "catalog:read", "files:read" }; return Results.Json(new { accessToken = token, tokenType = "Bearer", scopes });
}).RequireRateLimiting("auth");

app.MapGet("/api/v1/catalog", async (HttpContext ctx, HomeStorageDb db, AuthService auth, ILogger<Program> log, string? parentId, string? cursor, int? limit) =>
{
    var identity = await auth.AuthenticateAsync(ctx, true, ctx.RequestAborted); if (identity is null) { log.LogWarning("Authorization failed for catalog from {RemoteIp}", ctx.Connection.RemoteIpAddress); return Results.Unauthorized(); }
    var take = Math.Clamp(limit ?? 100, 1, 200); var offset = Cursor.Decode(cursor); var parent = string.IsNullOrEmpty(parentId) || parentId == "root" ? null : parentId;
    if (parent is not null && !await db.Catalog.AnyAsync(x => x.Id == parent && x.Kind == CatalogKind.Folder && x.Active && !x.Suppressed)) return Results.NotFound();
    var query = db.Catalog.AsNoTracking().Where(x => x.ParentId == parent && x.Active && !x.Suppressed).OrderBy(x => x.Kind == CatalogKind.File).ThenBy(x => x.Name);
    var rows = await query.Skip(offset).Take(take + 1).ToListAsync(); var more = rows.Count > take; if (more) rows.RemoveAt(take);
    return Results.Json(new { items = rows.Select(x => new { id = x.Id, parentId = x.ParentId ?? "root", kind = x.Kind == CatalogKind.File ? "file" : "folder", name = x.Name, extension = x.Extension, size = x.Size.ToString(), modifiedAt = new DateTimeOffset(x.ModifiedUtcTicks, TimeSpan.Zero), etag = x.ETag, sha256 = x.Sha256, canDownload = x.Kind == CatalogKind.File, canHide = identity.CanManageCatalog }), nextCursor = more ? Cursor.Encode(offset + take) : null });
});

app.MapMethods("/api/v1/files/{fileId}/content", new[] { "GET", "HEAD" }, async (HttpContext ctx, string fileId, HomeStorageDb db, AuthService auth, LibraryPathPolicy paths, DownloadGate gate, ScanTrigger trigger, ILogger<Program> log) =>
{
    var identity = await auth.AuthenticateAsync(ctx, true, ctx.RequestAborted); if (identity is null) { log.LogWarning("Authorization failed for download {FileId} from {RemoteIp}", fileId, ctx.Connection.RemoteIpAddress); return Results.Unauthorized(); }
    var entry = await db.Catalog.AsNoTracking().SingleOrDefaultAsync(x => x.Id == fileId && x.Kind == CatalogKind.File && x.Active && !x.Suppressed); if (entry is null) return Results.NotFound();
    var settings = await db.Settings.AsNoTracking().SingleAsync(); if (!paths.TryResolve(settings.LibraryPath, entry.RelativePath, out var path) || !File.Exists(path)) return Results.NotFound();
    var info = new FileInfo(path); if (info.Length != entry.Size || info.LastWriteTimeUtc.Ticks != entry.ModifiedUtcTicks) { trigger.Request(); return Results.Conflict(); }
    if (HttpMethods.IsHead(ctx.Request.Method))
    {
        ctx.Response.StatusCode = StatusCodes.Status200OK; ctx.Response.ContentType = "application/octet-stream"; ctx.Response.ContentLength = entry.Size;
        ctx.Response.Headers.AcceptRanges = "bytes"; ctx.Response.Headers.ETag = entry.ETag; ctx.Response.Headers.LastModified = new DateTimeOffset(entry.ModifiedUtcTicks, TimeSpan.Zero).ToString("R"); ctx.Response.Headers.CacheControl = "private, no-store";
        return Results.Empty;
    }
    Stream? stream; try { stream = await gate.OpenAsync(path, ctx.RequestAborted); } catch (Exception ex) { log.LogError(ex, "Filesystem error opening {FileId}", fileId); return Results.Problem(statusCode: 500); }
    if (stream is null) return Results.StatusCode(StatusCodes.Status429TooManyRequests);
    var range = ctx.Request.Headers.Range.ToString(); log.LogInformation("Download started {FileId} {Method} range {Range}", fileId, ctx.Request.Method, string.IsNullOrEmpty(range) ? "none" : range);
    var cancellationLogged = 0;
    ctx.Response.Headers.CacheControl = "private, no-store"; ctx.RequestAborted.Register(() => { if (Interlocked.Exchange(ref cancellationLogged, 1) == 0) log.LogWarning("Download canceled {FileId}", fileId); });
    ctx.Response.OnCompleted(() =>
    {
        if (Volatile.Read(ref cancellationLogged) != 0) return Task.CompletedTask;
        if (ctx.Response.StatusCode is >= 200 and < 300) log.LogInformation("Download completed {FileId} with status {Status}", fileId, ctx.Response.StatusCode);
        else log.LogWarning("Download failed {FileId} with status {Status}", fileId, ctx.Response.StatusCode);
        return Task.CompletedTask;
    });
    return Results.File(stream, "application/octet-stream", entry.Name, new DateTimeOffset(entry.ModifiedUtcTicks, TimeSpan.Zero), EntityTagHeaderValue.Parse(entry.ETag), enableRangeProcessing: true);
});

app.MapDelete("/api/v1/catalog/{id}", async (HttpContext ctx, string id, HomeStorageDb db, AuthService auth, ILogger<Program> log) =>
{
    var identity = await auth.AuthenticateAsync(ctx, false, ctx.RequestAborted); if (identity is null) { log.LogWarning("Authorization failed for catalog management from {RemoteIp}", ctx.Connection.RemoteIpAddress); return Results.Unauthorized(); }
    if (!identity.CanManageCatalog) { log.LogWarning("Catalog management denied for token {TokenId}", identity.TokenId); return Results.Forbid(); }
    var entry = await db.Catalog.SingleOrDefaultAsync(x => x.Id == id && x.Active); if (entry is null) return Results.NotFound();
    if (entry.Kind == CatalogKind.Folder) { var prefix = entry.RelativePath + "/"; foreach (var child in await db.Catalog.Where(x => x.RelativePath == entry.RelativePath || x.RelativePath.StartsWith(prefix)).ToListAsync()) child.Suppressed = true; } else entry.Suppressed = true;
    await db.SaveChangesAsync(); return Results.NoContent();
});

AdminPanel.Map(app, runtime);
app.Run();

public partial class Program { }

static class Cursor
{
    public static int Decode(string? value) { if (string.IsNullOrEmpty(value)) return 0; try { var text = Encoding.ASCII.GetString(WebEncoders.Base64UrlDecode(value)); return int.TryParse(text, out var n) && n >= 0 ? n : 0; } catch { return 0; } }
    public static string Encode(int value) => WebEncoders.Base64UrlEncode(Encoding.ASCII.GetBytes(value.ToString()));
}
