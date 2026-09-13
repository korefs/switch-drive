using Microsoft.EntityFrameworkCore;

namespace HomeStorage;

public enum CatalogKind { Folder, File }

public sealed class CatalogEntry
{
    public string Id { get; set; } = Guid.NewGuid().ToString("N");
    public string? ParentId { get; set; }
    public CatalogKind Kind { get; set; }
    public string RelativePath { get; set; } = "";
    public string Name { get; set; } = "";
    public string Extension { get; set; } = "";
    public long Size { get; set; }
    public long ModifiedUtcTicks { get; set; }
    public string Sha256 { get; set; } = "";
    public string ETag { get; set; } = "";
    public bool Active { get; set; } = true;
    public bool Suppressed { get; set; }
    public string LastSeenScanId { get; set; } = "";
    public string? TitleId { get; set; }
    public string? Title { get; set; }
    public string? Publisher { get; set; }
    public long? Version { get; set; }
    public string? ContentType { get; set; }
    public long? RequiredFirmware { get; set; }
    public string? RelatedBaseTitleId { get; set; }
    public string? IconRelativePath { get; set; }
}

public sealed class ServiceSettings
{
    public int Id { get; set; } = 1;
    public string InstanceId { get; set; } = Guid.NewGuid().ToString("N");
    public string InstanceName { get; set; } = "Home Storage";
    public string LibraryPath { get; set; } = "/library";
    public bool AuthRequired { get; set; } = true;
    public DateTimeOffset? LastScanAt { get; set; }
    public string LastScanStatus { get; set; } = "pending";
    public string? LastScanError { get; set; }
}

public sealed class AdminCredential { public int Id { get; set; } = 1; public string Username { get; set; } = ""; public string PasswordHash { get; set; } = ""; }
public sealed class LibraryCredential { public int Id { get; set; } = 1; public string Username { get; set; } = ""; public string PasswordHash { get; set; } = ""; public bool AllowCatalogManage { get; set; } public int Version { get; set; } = 1; }
public sealed class DeviceToken
{
    public string Id { get; set; } = Guid.NewGuid().ToString("N");
    public string TokenHash { get; set; } = "";
    public string Name { get; set; } = "Nintendo Switch";
    public bool CanManageCatalog { get; set; }
    public int CredentialVersion { get; set; }
    public DateTimeOffset CreatedAt { get; set; } = DateTimeOffset.UtcNow;
    public DateTimeOffset? LastUsedAt { get; set; }
    public DateTimeOffset? RevokedAt { get; set; }
}

public sealed class HomeStorageDb(DbContextOptions<HomeStorageDb> options) : DbContext(options)
{
    public DbSet<CatalogEntry> Catalog => Set<CatalogEntry>();
    public DbSet<ServiceSettings> Settings => Set<ServiceSettings>();
    public DbSet<AdminCredential> Admins => Set<AdminCredential>();
    public DbSet<LibraryCredential> LibraryCredentials => Set<LibraryCredential>();
    public DbSet<DeviceToken> DeviceTokens => Set<DeviceToken>();
    protected override void OnModelCreating(ModelBuilder model)
    {
        model.Entity<CatalogEntry>(e => { e.HasKey(x => x.Id); e.HasIndex(x => x.RelativePath).IsUnique(); e.HasIndex(x => new { x.ParentId, x.Active, x.Suppressed }); e.HasIndex(x => new { x.Sha256, x.Size }); e.Property(x => x.Kind).HasConversion<string>(); });
        model.Entity<ServiceSettings>().HasKey(x => x.Id); model.Entity<AdminCredential>().HasKey(x => x.Id);
        model.Entity<LibraryCredential>().HasKey(x => x.Id); model.Entity<DeviceToken>().HasKey(x => x.Id);
        model.Entity<DeviceToken>().HasIndex(x => x.TokenHash).IsUnique();
    }
}

public sealed record RuntimeOptions(string LibraryMountRoot, string InitialLibraryPath, string DataPath, int HttpPort, int DiscoveryPort, int ScanIntervalMinutes, int MaxConcurrentDownloads, string InitialInstanceName, string SetupToken)
{
    public static RuntimeOptions FromConfiguration(IConfiguration c)
    {
        static int N(IConfiguration c, string key, int fallback, int min, int max) => int.TryParse(c[key], out var value) ? Math.Clamp(value, min, max) : fallback;
        return new(c["LIBRARY_MOUNT_ROOT"] ?? "/library", c["LIBRARY_PATH"] ?? "/library", c["DATA_PATH"] ?? "/data", N(c, "HOME_STORAGE_PORT", 8080, 1, 65535), N(c, "DISCOVERY_PORT", 8080, 1, 65535), N(c, "SCAN_INTERVAL_MINUTES", 5, 1, 1440), N(c, "MAX_CONCURRENT_DOWNLOADS", 4, 1, 32), c["INSTANCE_NAME"] ?? "Home Storage", c["SETUP_TOKEN"] ?? "");
    }
}

public sealed class ScanState { private int scanning; public bool IsScanning => Volatile.Read(ref scanning) != 0; public bool TryBegin() => Interlocked.CompareExchange(ref scanning, 1, 0) == 0; public void End() => Volatile.Write(ref scanning, 0); }
public interface IFileMetadataExtractor { bool Supports(string extension); Task ExtractAsync(CatalogEntry entry, Stream stream, CancellationToken cancellationToken); }
public sealed class MetadataPipeline(IEnumerable<IFileMetadataExtractor> extractors)
{
    public async Task ExtractAsync(CatalogEntry entry, string path, CancellationToken ct) { foreach (var x in extractors.Where(x => x.Supports(entry.Extension))) { await using var stream = new FileStream(path, FileMode.Open, FileAccess.Read, FileShare.Read, 128 * 1024, FileOptions.Asynchronous | FileOptions.SequentialScan); await x.ExtractAsync(entry, stream, ct); } }
}
