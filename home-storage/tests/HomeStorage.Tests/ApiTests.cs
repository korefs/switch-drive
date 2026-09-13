using System.Net;
using System.Net.Http.Headers;
using System.Text;
using System.Text.Json;
using System.Text.RegularExpressions;
using HomeStorage;
using Microsoft.AspNetCore.Hosting;
using Microsoft.AspNetCore.Mvc.Testing;
using Microsoft.EntityFrameworkCore;
using Microsoft.Extensions.DependencyInjection;
using Xunit;

namespace HomeStorage.Tests;

public sealed class ApiTests : IDisposable
{
    private readonly string root = Path.Combine(Path.GetTempPath(), "home-storage-api-" + Guid.NewGuid().ToString("N"));
    private readonly string data = Path.Combine(Path.GetTempPath(), "home-storage-data-" + Guid.NewGuid().ToString("N"));
    public ApiTests() { Directory.CreateDirectory(root); Directory.CreateDirectory(data); }

    [Fact]
    public async Task StreamsHeadAndRangesFromSparseLargeFile()
    {
        var length = OperatingSystem.IsWindows() ? 16L * 1024 * 1024 : 35L * 1024 * 1024 * 1024;
        var path = Path.Combine(root, "large.bin"); await using (var file = new FileStream(path, FileMode.CreateNew, FileAccess.Write, FileShare.Read)) { file.SetLength(length); file.Position = file.Length - 1; file.WriteByte(0x5a); }
        await using var factory = Factory(); var client = factory.CreateClient();
        string id;
        using (var scope = factory.Services.CreateScope()) { var db = scope.ServiceProvider.GetRequiredService<HomeStorageDb>(); var settings = await db.Settings.SingleAsync(); settings.AuthRequired = false; var info = new FileInfo(path); var entry = new CatalogEntry { Kind = CatalogKind.File, RelativePath = "large.bin", Name = "large.bin", Extension = ".bin", Size = info.Length, ModifiedUtcTicks = info.LastWriteTimeUtc.Ticks, Sha256 = new string('a', 64), ETag = "\"sha256-" + new string('a', 64) + "\"", Active = true }; id = entry.Id; db.Catalog.Add(entry); await db.SaveChangesAsync(); }
        using var head = new HttpRequestMessage(HttpMethod.Head, $"/api/v1/files/{id}/content"); using var headResponse = await client.SendAsync(head, HttpCompletionOption.ResponseHeadersRead); Assert.Equal(HttpStatusCode.OK, headResponse.StatusCode); Assert.Equal(length, headResponse.Content.Headers.ContentLength); Assert.Empty(await headResponse.Content.ReadAsByteArrayAsync()); Assert.Contains("bytes", headResponse.Headers.AcceptRanges); Assert.NotNull(headResponse.Headers.ETag); Assert.NotNull(headResponse.Content.Headers.LastModified); var etag = headResponse.Headers.ETag!;
        using var request = new HttpRequestMessage(HttpMethod.Get, $"/api/v1/files/{id}/content"); request.Headers.Range = new RangeHeaderValue(length - 16, null); using var response = await client.SendAsync(request, HttpCompletionOption.ResponseHeadersRead); Assert.Equal(HttpStatusCode.PartialContent, response.StatusCode); Assert.Equal(16, response.Content.Headers.ContentLength); Assert.Equal(length - 16, response.Content.Headers.ContentRange!.From); var bytes = await response.Content.ReadAsByteArrayAsync(); Assert.Equal(16, bytes.Length); Assert.Equal(0x5a, bytes[^1]);
        using var suffix = new HttpRequestMessage(HttpMethod.Get, $"/api/v1/files/{id}/content"); suffix.Headers.Range = new RangeHeaderValue(null, 4); using var suffixResponse = await client.SendAsync(suffix, HttpCompletionOption.ResponseHeadersRead); Assert.Equal(HttpStatusCode.PartialContent, suffixResponse.StatusCode); Assert.Equal(4, suffixResponse.Content.Headers.ContentLength); Assert.Equal(length - 4, suffixResponse.Content.Headers.ContentRange!.From);
        using var resumed = new HttpRequestMessage(HttpMethod.Get, $"/api/v1/files/{id}/content"); resumed.Headers.Range = new RangeHeaderValue(0, 7); resumed.Headers.IfRange = new RangeConditionHeaderValue(etag); using var resumedResponse = await client.SendAsync(resumed, HttpCompletionOption.ResponseHeadersRead); Assert.Equal(HttpStatusCode.PartialContent, resumedResponse.StatusCode); Assert.Equal(8, resumedResponse.Content.Headers.ContentLength);
        using var invalid = new HttpRequestMessage(HttpMethod.Get, $"/api/v1/files/{id}/content"); invalid.Headers.Range = new RangeHeaderValue(length + 1024, null); using var invalidResponse = await client.SendAsync(invalid); Assert.Equal(HttpStatusCode.RequestedRangeNotSatisfiable, invalidResponse.StatusCode);
    }

    [Fact]
    public async Task StaleIfRangeFallsBackToAFullSmallResponse()
    {
        var path = Path.Combine(root, "small.zip"); await File.WriteAllBytesAsync(path, Enumerable.Range(0, 32).Select(x => (byte)x).ToArray(), TestContext.Current.CancellationToken); await using var factory = Factory(); var client = factory.CreateClient(); string id;
        using (var scope = factory.Services.CreateScope()) { var db = scope.ServiceProvider.GetRequiredService<HomeStorageDb>(); (await db.Settings.SingleAsync()).AuthRequired = false; var info = new FileInfo(path); var hash = new string('c', 64); var entry = new CatalogEntry { Kind = CatalogKind.File, RelativePath = "small.zip", Name = "small.zip", Extension = ".zip", Size = info.Length, ModifiedUtcTicks = info.LastWriteTimeUtc.Ticks, Sha256 = hash, ETag = "\"sha256-" + hash + "\"", Active = true }; id = entry.Id; db.Catalog.Add(entry); await db.SaveChangesAsync(); }
        using var stale = new HttpRequestMessage(HttpMethod.Get, $"/api/v1/files/{id}/content"); stale.Headers.Range = new RangeHeaderValue(0, 7); stale.Headers.IfRange = new RangeConditionHeaderValue(new EntityTagHeaderValue("\"different\"")); using var response = await client.SendAsync(stale); Assert.Equal(HttpStatusCode.OK, response.StatusCode); Assert.Equal(32, response.Content.Headers.ContentLength); Assert.Equal(32, (await response.Content.ReadAsByteArrayAsync(TestContext.Current.CancellationToken)).Length);
    }

    [Fact]
    public async Task IssuesRevocableBearerAndEnforcesCatalogManageScope()
    {
        await using var factory = Factory(); var client = factory.CreateClient(); string id;
        using (var scope = factory.Services.CreateScope()) { var db = scope.ServiceProvider.GetRequiredService<HomeStorageDb>(); var auth = scope.ServiceProvider.GetRequiredService<AuthService>(); db.LibraryCredentials.Add(new() { Username = "switch", PasswordHash = auth.HashPassword("switch", "correct-password"), AllowCatalogManage = true }); var entry = new CatalogEntry { Kind = CatalogKind.File, RelativePath = "hidden.bin", Name = "hidden.bin", Active = true }; id = entry.Id; db.Catalog.Add(entry); await db.SaveChangesAsync(); }
        using var bad = new HttpRequestMessage(HttpMethod.Post, "/api/v1/auth/token"); bad.Headers.Authorization = new AuthenticationHeaderValue("Basic", Convert.ToBase64String(Encoding.UTF8.GetBytes("switch:wrong-password"))); Assert.Equal(HttpStatusCode.Unauthorized, (await client.SendAsync(bad)).StatusCode);
        using var login = new HttpRequestMessage(HttpMethod.Post, "/api/v1/auth/token"); login.Headers.Authorization = new AuthenticationHeaderValue("Basic", Convert.ToBase64String(Encoding.UTF8.GetBytes("switch:correct-password"))); using var loginResponse = await client.SendAsync(login); Assert.Equal(HttpStatusCode.OK, loginResponse.StatusCode); var json = JsonDocument.Parse(await loginResponse.Content.ReadAsStringAsync()); var token = json.RootElement.GetProperty("accessToken").GetString(); Assert.False(string.IsNullOrEmpty(token)); Assert.Contains(json.RootElement.GetProperty("scopes").EnumerateArray(), x => x.GetString() == "catalog:manage");
        using var hide = new HttpRequestMessage(HttpMethod.Delete, $"/api/v1/catalog/{id}"); hide.Headers.Authorization = new AuthenticationHeaderValue("Bearer", token); Assert.Equal(HttpStatusCode.NoContent, (await client.SendAsync(hide)).StatusCode);
        using (var scope2 = factory.Services.CreateScope()) { var db = scope2.ServiceProvider.GetRequiredService<HomeStorageDb>(); Assert.True((await db.Catalog.SingleAsync(x => x.Id == id)).Suppressed); var device = await db.DeviceTokens.SingleAsync(); Assert.NotEqual(token, device.TokenHash); device.RevokedAt = DateTimeOffset.UtcNow; await db.SaveChangesAsync(); }
        using var afterRevoke = new HttpRequestMessage(HttpMethod.Get, "/api/v1/catalog?parentId=root"); afterRevoke.Headers.Authorization = new AuthenticationHeaderValue("Bearer", token); Assert.Equal(HttpStatusCode.Unauthorized, (await client.SendAsync(afterRevoke)).StatusCode);
    }

    [Fact]
    public async Task RefusesAFileThatChangedAfterIndexing()
    {
        var path = Path.Combine(root, "changed.nsp"); await File.WriteAllTextAsync(path, "before"); await using var factory = Factory(); var client = factory.CreateClient(); string id;
        using (var scope = factory.Services.CreateScope()) { var db = scope.ServiceProvider.GetRequiredService<HomeStorageDb>(); (await db.Settings.SingleAsync()).AuthRequired = false; var info = new FileInfo(path); var entry = new CatalogEntry { Kind = CatalogKind.File, RelativePath = "changed.nsp", Name = "changed.nsp", Extension = ".nsp", Size = info.Length, ModifiedUtcTicks = info.LastWriteTimeUtc.Ticks, Sha256 = new string('b', 64), ETag = "\"sha256-" + new string('b', 64) + "\"", Active = true }; id = entry.Id; db.Catalog.Add(entry); await db.SaveChangesAsync(); }
        await File.AppendAllTextAsync(path, "-changed"); using var response = await client.GetAsync($"/api/v1/files/{id}/content"); Assert.Equal(HttpStatusCode.Conflict, response.StatusCode);
    }

    [Fact]
    public async Task DownloadGateKeepsTheConfiguredConcurrencyBound()
    {
        var path = Path.Combine(root, "gate.bin"); await File.WriteAllBytesAsync(path, new byte[16]); var gate = new DownloadGate(new(root, root, data, 8080, 8080, 5, 1, "test", "setup"));
        await using var first = await gate.OpenAsync(path, TestContext.Current.CancellationToken); Assert.NotNull(first); Assert.Null(await gate.OpenAsync(path, TestContext.Current.CancellationToken));
        await first.DisposeAsync(); await using var reopened = await gate.OpenAsync(path, TestContext.Current.CancellationToken); Assert.NotNull(reopened);
    }

    [Fact]
    public async Task ScannerKeepsOpaqueIdAcrossRenameAndMarksRemovalInactive()
    {
        var games = Path.Combine(root, "Games"); Directory.CreateDirectory(games); var first = Path.Combine(games, "first.nsp"); await File.WriteAllTextAsync(first, "package"); await File.WriteAllTextAsync(Path.Combine(root, "ignored.txt"), "ignore");
        await using var factory = Factory(); _ = factory.CreateClient(); var indexer = factory.Services.GetRequiredService<CatalogIndexer>(); await indexer.ScanAsync(TestContext.Current.CancellationToken); string id;
        using (var scope = factory.Services.CreateScope()) { var db = scope.ServiceProvider.GetRequiredService<HomeStorageDb>(); var file = await db.Catalog.SingleAsync(x => x.Kind == CatalogKind.File); id = file.Id; Assert.Equal(64, file.Sha256.Length); Assert.Contains("sha256-", file.ETag); Assert.Single(await db.Catalog.Where(x => x.Kind == CatalogKind.Folder).ToListAsync()); }
        var renamed = Path.Combine(games, "renamed.nsp"); File.Move(first, renamed); await indexer.ScanAsync(TestContext.Current.CancellationToken);
        using (var scope = factory.Services.CreateScope()) { var db = scope.ServiceProvider.GetRequiredService<HomeStorageDb>(); var file = await db.Catalog.SingleAsync(x => x.Kind == CatalogKind.File); Assert.Equal(id, file.Id); Assert.Equal("Games/renamed.nsp", file.RelativePath); Assert.True(file.Active); file.Suppressed = true; await db.SaveChangesAsync(); }
        await indexer.ScanAsync(TestContext.Current.CancellationToken); using (var scope = factory.Services.CreateScope()) { Assert.True((await scope.ServiceProvider.GetRequiredService<HomeStorageDb>().Catalog.SingleAsync(x => x.Id == id)).Suppressed); }
        File.Delete(renamed); await indexer.ScanAsync(TestContext.Current.CancellationToken); using var last = factory.Services.CreateScope(); Assert.False((await last.ServiceProvider.GetRequiredService<HomeStorageDb>().Catalog.SingleAsync(x => x.Id == id)).Active);
    }

    [Fact]
    public async Task FirstSetupRequiresTokenAndStoresOnlyPasswordHashes()
    {
        await using var factory = Factory(); var client = factory.CreateClient(new WebApplicationFactoryClientOptions { AllowAutoRedirect = false, HandleCookies = true });
        var setup = await client.GetStringAsync("/setup", TestContext.Current.CancellationToken); var csrf = Regex.Match(setup, "name=__RequestVerificationToken value=\"([^\"]+)\"").Groups[1].Value; Assert.NotEmpty(csrf);
        var fields = new Dictionary<string, string> { { "__RequestVerificationToken", csrf }, { "setupToken", "test-only-setup-token" }, { "admin", "admin" }, { "adminPassword", "administrator-password" }, { "instanceName", "Test Storage" }, { "libraryPath", root }, { "libraryUser", "switch" }, { "libraryPassword", "library-password" }, { "allowManage", "on" } };
        using var response = await client.PostAsync("/setup", new FormUrlEncodedContent(fields), TestContext.Current.CancellationToken); Assert.Equal(HttpStatusCode.Redirect, response.StatusCode);
        using var scope = factory.Services.CreateScope(); var db = scope.ServiceProvider.GetRequiredService<HomeStorageDb>(); var admin = await db.Admins.SingleAsync(TestContext.Current.CancellationToken); var library = await db.LibraryCredentials.SingleAsync(TestContext.Current.CancellationToken);
        Assert.DoesNotContain("administrator-password", admin.PasswordHash); Assert.DoesNotContain("library-password", library.PasswordHash); Assert.True(library.AllowCatalogManage);
    }

    private WebApplicationFactory<Program> Factory()
    {
        var port = Random.Shared.Next(20000, 50000); return new WebApplicationFactory<Program>().WithWebHostBuilder(builder => builder.UseSetting("LIBRARY_MOUNT_ROOT", root).UseSetting("LIBRARY_PATH", root).UseSetting("DATA_PATH", data).UseSetting("HOME_STORAGE_PORT", port.ToString()).UseSetting("DISCOVERY_PORT", port.ToString()).UseSetting("SETUP_TOKEN", "test-only-setup-token").UseSetting("DISABLE_BACKGROUND_SERVICES", "true"));
    }
    public void Dispose() { try { Directory.Delete(root, true); } catch { } try { Directory.Delete(data, true); } catch { } }
}
