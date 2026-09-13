using System.Security.Cryptography;
using Microsoft.EntityFrameworkCore;

namespace HomeStorage;

public sealed class ScanTrigger
{
    private readonly SemaphoreSlim signal = new(0, 1);
    public void Request() { if (signal.CurrentCount == 0) signal.Release(); }
    public Task WaitAsync(CancellationToken ct) => signal.WaitAsync(ct);
}

public sealed class CatalogIndexer(IDbContextFactory<HomeStorageDb> factory, LibraryPathPolicy paths, MetadataPipeline metadata, ScanState state, ILogger<CatalogIndexer> logger)
{
    private static readonly HashSet<string> Supported = new(StringComparer.OrdinalIgnoreCase) { ".nro", ".nsp", ".nsz", ".xci", ".zip" };
    public async Task ScanAsync(CancellationToken ct)
    {
        if (!state.TryBegin()) return; var scanId = Guid.NewGuid().ToString("N");
        try
        {
            await using var db = await factory.CreateDbContextAsync(ct); var settings = await db.Settings.SingleAsync(ct); settings.LastScanStatus = "scanning"; settings.LastScanError = null; await db.SaveChangesAsync(ct);
            if (!paths.TryValidateLibraryPath(settings.LibraryPath, out var root, out var error)) throw new IOException(error);
            logger.LogInformation("Library scan {ScanId} started at {LibraryPath}", scanId, settings.LibraryPath);
            var directories = new List<string>(); var files = new List<string>(); Walk(root, root, directories, files);
            var byPath = await db.Catalog.ToDictionaryAsync(x => x.RelativePath, StringComparer.Ordinal, ct); var seen = new HashSet<string>(StringComparer.Ordinal); var parentIds = new Dictionary<string, string>(StringComparer.Ordinal) { { "", "root" } };
            foreach (var directory in directories.OrderBy(x => x.Count(c => c == '/')))
            {
                ct.ThrowIfCancellationRequested(); var relative = Relative(root, directory); var parent = Parent(relative); var entry = byPath.GetValueOrDefault(relative);
                if (entry is null) { entry = new() { Kind = CatalogKind.Folder, RelativePath = relative, Name = Path.GetFileName(directory) }; db.Catalog.Add(entry); byPath[relative] = entry; logger.LogInformation("Folder indexed {RelativePath}", relative); }
                entry.Kind = CatalogKind.Folder; entry.ParentId = parent.Length == 0 ? null : parentIds[parent]; entry.Active = true; entry.LastSeenScanId = scanId; parentIds[relative] = entry.Id; seen.Add(relative);
            }
            await db.SaveChangesAsync(ct);
            foreach (var file in files)
            {
                ct.ThrowIfCancellationRequested(); var info = new FileInfo(file); var relative = Relative(root, file); var parent = Parent(relative); var extension = info.Extension.ToLowerInvariant(); var entry = byPath.GetValueOrDefault(relative);
                var changed = entry is null || entry.Size != info.Length || entry.ModifiedUtcTicks != info.LastWriteTimeUtc.Ticks || string.IsNullOrEmpty(entry.Sha256);
                string? hash = null; if (changed) hash = await HashAsync(file, ct);
                if (entry is null && hash is not null)
                {
                    var candidates = await db.Catalog.Where(x => x.Kind == CatalogKind.File && x.RelativePath != relative && x.Size == info.Length && x.Sha256 == hash).ToListAsync(ct);
                    var unique = candidates.Where(x => !seen.Contains(x.RelativePath) && !File.Exists(Path.Combine(root, x.RelativePath))).ToList();
                    if (unique.Count == 1) { entry = unique[0]; byPath.Remove(entry.RelativePath); entry.RelativePath = relative; byPath[relative] = entry; logger.LogInformation("File moved, retaining {FileId}: {RelativePath}", entry.Id, relative); }
                }
                if (entry is null) { entry = new() { Kind = CatalogKind.File, RelativePath = relative }; db.Catalog.Add(entry); byPath[relative] = entry; logger.LogInformation("File found {FileId}: {RelativePath}", entry.Id, relative); }
                entry.Kind = CatalogKind.File; entry.ParentId = parent.Length == 0 ? null : parentIds.GetValueOrDefault(parent); entry.Name = info.Name; entry.Extension = extension; entry.Size = info.Length; entry.ModifiedUtcTicks = info.LastWriteTimeUtc.Ticks;
                if (hash is not null) { entry.Sha256 = hash; entry.ETag = "\"sha256-" + hash + "\""; await metadata.ExtractAsync(entry, file, ct); }
                entry.Active = true; entry.LastSeenScanId = scanId; seen.Add(relative);
            }
            await db.SaveChangesAsync(ct);
            foreach (var entry in await db.Catalog.Where(x => x.LastSeenScanId != scanId && x.Active).ToListAsync(ct)) { entry.Active = false; logger.LogInformation("Catalog entry removed {FileId}: {RelativePath}", entry.Id, entry.RelativePath); }
            settings.LastScanAt = DateTimeOffset.UtcNow; settings.LastScanStatus = "ready"; settings.LastScanError = null; await db.SaveChangesAsync(ct);
            logger.LogInformation("Library scan {ScanId} completed with {Files} files", scanId, files.Count);
        }
        catch (OperationCanceledException) when (ct.IsCancellationRequested) { }
        catch (Exception ex) { logger.LogError(ex, "Library scan {ScanId} failed", scanId); await using var db = await factory.CreateDbContextAsync(CancellationToken.None); var settings = await db.Settings.SingleAsync(); settings.LastScanAt = DateTimeOffset.UtcNow; settings.LastScanStatus = "degraded"; settings.LastScanError = ex.Message; await db.SaveChangesAsync(); }
        finally { state.End(); }
    }
    private void Walk(string root, string directory, List<string> directories, List<string> files)
    {
        string[] entries; try { entries = Directory.GetFileSystemEntries(directory); } catch (Exception ex) { logger.LogError(ex, "Cannot enumerate {RelativePath}", Relative(root, directory)); return; }
        foreach (var path in entries) { var relative = Relative(root, path); if (relative.Contains('\\')) { logger.LogWarning("Skipping path with an invalid separator {RelativePath}", relative); continue; } if (paths.IsReparsePoint(path)) { logger.LogWarning("Skipping symbolic link or reparse point {RelativePath}", relative); continue; } if (Directory.Exists(path)) { directories.Add(path); Walk(root, path, directories, files); } else if (File.Exists(path) && Supported.Contains(Path.GetExtension(path))) files.Add(path); }
    }
    private static string Relative(string root, string path) => Path.GetRelativePath(root, path).Replace(Path.DirectorySeparatorChar, '/');
    private static string Parent(string path) { var index = path.LastIndexOf('/'); return index < 0 ? "" : path[..index]; }
    private static async Task<string> HashAsync(string path, CancellationToken ct) { await using var stream = new FileStream(path, FileMode.Open, FileAccess.Read, FileShare.Read, 1024 * 1024, FileOptions.Asynchronous | FileOptions.SequentialScan); var hash = await SHA256.HashDataAsync(stream, ct); return Convert.ToHexString(hash).ToLowerInvariant(); }
}

public sealed class CatalogIndexerService(CatalogIndexer indexer, ScanTrigger trigger, RuntimeOptions options) : BackgroundService
{
    protected override async Task ExecuteAsync(CancellationToken stoppingToken)
    {
        await indexer.ScanAsync(stoppingToken);
        while (!stoppingToken.IsCancellationRequested)
        {
            using var wakeup = CancellationTokenSource.CreateLinkedTokenSource(stoppingToken);
            var delay = Task.Delay(TimeSpan.FromMinutes(options.ScanIntervalMinutes), wakeup.Token); var requested = trigger.WaitAsync(wakeup.Token);
            try { await Task.WhenAny(delay, requested); wakeup.Cancel(); try { await Task.WhenAll(delay, requested); } catch (OperationCanceledException) { } await indexer.ScanAsync(stoppingToken); } catch (OperationCanceledException) when (stoppingToken.IsCancellationRequested) { }
        }
    }
}
