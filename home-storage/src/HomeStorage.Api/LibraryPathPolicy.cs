namespace HomeStorage;

public sealed class LibraryPathPolicy(RuntimeOptions options)
{
    private readonly string mountRoot = Canonical(options.LibraryMountRoot);
    private readonly StringComparison comparison = OperatingSystem.IsWindows() ? StringComparison.OrdinalIgnoreCase : StringComparison.Ordinal;
    public string MountRoot => mountRoot;

    public bool TryValidateLibraryPath(string value, out string canonical, out string error)
    {
        canonical = error = "";
        try { canonical = Canonical(value); } catch (Exception ex) { error = ex.Message; return false; }
        if (!Contains(mountRoot, canonical)) { error = "Library path must be inside the mounted root."; return false; }
        if (!Directory.Exists(canonical)) { error = "Library directory does not exist or is not readable."; return false; }
        if (HasReparseComponent(mountRoot, canonical)) { error = "Symbolic links and reparse points are not allowed."; return false; }
        return true;
    }

    public bool TryResolve(string libraryPath, string relativePath, out string fullPath)
    {
        fullPath = "";
        if (string.IsNullOrWhiteSpace(relativePath) || Path.IsPathRooted(relativePath) || relativePath.Contains('\\') || relativePath.StartsWith('/') || relativePath.EndsWith('/') || relativePath.Contains("//", StringComparison.Ordinal)) return false;
        var segments = relativePath.Split('/');
        if (segments.Length == 0 || segments.Any(x => x.Length == 0 || x is "." or "..")) return false;
        if (!TryValidateLibraryPath(libraryPath, out var root, out _)) return false;
        try { fullPath = Canonical(Path.Combine(root, Path.Combine(segments))); } catch { return false; }
        return Contains(root, fullPath) && !HasReparseComponent(root, fullPath);
    }

    public bool IsReparsePoint(string path) { try { return (File.GetAttributes(path) & FileAttributes.ReparsePoint) != 0; } catch { return true; } }
    private bool Contains(string root, string candidate) => candidate.Equals(root, comparison) || candidate.StartsWith(root + Path.DirectorySeparatorChar, comparison);
    private bool HasReparseComponent(string root, string candidate)
    {
        if (IsReparsePoint(root)) return true;
        var relative = Path.GetRelativePath(root, candidate); if (relative == ".") return false;
        var current = root;
        foreach (var segment in relative.Split(new[] { Path.DirectorySeparatorChar, Path.AltDirectorySeparatorChar }, StringSplitOptions.RemoveEmptyEntries)) { current = Path.Combine(current, segment); if ((Directory.Exists(current) || File.Exists(current)) && IsReparsePoint(current)) return true; }
        return false;
    }
    private static string Canonical(string path) => Path.TrimEndingDirectorySeparator(Path.GetFullPath(path));
}
