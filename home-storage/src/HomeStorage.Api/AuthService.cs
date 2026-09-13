using System.Security.Cryptography;
using System.Text;
using Microsoft.AspNetCore.Identity;
using Microsoft.EntityFrameworkCore;

namespace HomeStorage;

public sealed record ApiIdentity(bool Authenticated, bool CanManageCatalog, string? TokenId);
public sealed class AuthService(IDbContextFactory<HomeStorageDb> factory, IPasswordHasher<string> hasher)
{
    public string HashPassword(string username, string password) => hasher.HashPassword(username, password);
    public bool VerifyPassword(string username, string hash, string password) => hasher.VerifyHashedPassword(username, hash, password) != PasswordVerificationResult.Failed;

    public async Task<(string? Token, bool CanManage, string? Error)> ExchangeAsync(string username, string password, CancellationToken ct)
    {
        await using var db = await factory.CreateDbContextAsync(ct); var credential = await db.LibraryCredentials.SingleOrDefaultAsync(ct);
        if (credential is null || !FixedEquals(credential.Username, username) || !VerifyPassword(credential.Username, credential.PasswordHash, password)) return (null, false, "invalid_credentials");
        var token = Base64Url(RandomNumberGenerator.GetBytes(32)); db.DeviceTokens.Add(new() { TokenHash = HashToken(token), CanManageCatalog = credential.AllowCatalogManage, CredentialVersion = credential.Version }); await db.SaveChangesAsync(ct); return (token, credential.AllowCatalogManage, null);
    }
    public async Task<ApiIdentity?> AuthenticateAsync(HttpContext context, bool allowAnonymous, CancellationToken ct)
    {
        await using var db = await factory.CreateDbContextAsync(ct); var settings = await db.Settings.SingleAsync(ct);
        if (!settings.AuthRequired && allowAnonymous) return new(false, false, null);
        var header = context.Request.Headers.Authorization.ToString(); if (!header.StartsWith("Bearer ", StringComparison.OrdinalIgnoreCase)) return null;
        var raw = header[7..].Trim(); if (raw.Length < 32) return null; var hash = HashToken(raw);
        var token = await db.DeviceTokens.SingleOrDefaultAsync(x => x.TokenHash == hash && x.RevokedAt == null, ct); var credential = await db.LibraryCredentials.SingleOrDefaultAsync(ct);
        if (token is null || credential is null || token.CredentialVersion != credential.Version) return null;
        token.LastUsedAt = DateTimeOffset.UtcNow; await db.SaveChangesAsync(ct); return new(true, token.CanManageCatalog, token.Id);
    }
    public static bool TryReadBasic(HttpRequest request, out string username, out string password)
    {
        username = password = ""; var header = request.Headers.Authorization.ToString(); if (!header.StartsWith("Basic ", StringComparison.OrdinalIgnoreCase)) return false;
        try { var text = Encoding.UTF8.GetString(Convert.FromBase64String(header[6..].Trim())); var split = text.IndexOf(':'); if (split <= 0) return false; username = text[..split]; password = text[(split + 1)..]; return username.Length <= 128 && password.Length <= 1024; } catch { return false; }
    }
    public static string HashToken(string token) => Convert.ToHexString(SHA256.HashData(Encoding.UTF8.GetBytes(token))).ToLowerInvariant();
    private static bool FixedEquals(string a, string b) { var x = Encoding.UTF8.GetBytes(a); var y = Encoding.UTF8.GetBytes(b); return x.Length == y.Length && CryptographicOperations.FixedTimeEquals(x, y); }
    private static string Base64Url(byte[] bytes) => Convert.ToBase64String(bytes).TrimEnd('=').Replace('+', '-').Replace('/', '_');
}
