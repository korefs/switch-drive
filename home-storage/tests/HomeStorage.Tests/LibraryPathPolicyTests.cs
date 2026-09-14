using HomeStorage;
using Xunit;

namespace HomeStorage.Tests;

public sealed class LibraryPathPolicyTests : IDisposable
{
    private readonly string root = Path.Combine(Path.GetTempPath(), "home-storage-tests-" + Guid.NewGuid().ToString("N"));
    private LibraryPathPolicy Policy() => new(new(root, root, root, 8080, 8080, 5, 4, "test", "setup"));
    public LibraryPathPolicyTests() { Directory.CreateDirectory(Path.Combine(root, "Games")); File.WriteAllText(Path.Combine(root, "Games", "game.nsp"), "test"); }
    [Fact] public void ResolvesOnlyRelativeChildren() { var policy = Policy(); Assert.True(policy.TryResolve(root, "Games/game.nsp", out var path)); Assert.Equal(Path.Combine(root, "Games", "game.nsp"), path); Assert.False(policy.TryResolve(root, "../secret.nsp", out _)); Assert.False(policy.TryResolve(root, "Games\\game.nsp", out _)); Assert.False(policy.TryResolve(root, "Games//game.nsp", out _)); Assert.False(policy.TryResolve(root, Path.GetFullPath(Path.Combine(root, "..", "secret.nsp")), out _)); }
    [Fact] public void LibraryMustStayInsideMount() { var policy = Policy(); Assert.True(policy.TryValidateLibraryPath(Path.Combine(root, "Games"), out _, out _)); Assert.False(policy.TryValidateLibraryPath(Path.GetTempPath(), out _, out _)); }
    [Fact] public void RejectsSymlinks() { var target = Path.Combine(root, "Games"); var link = Path.Combine(root, "Linked"); try { Directory.CreateSymbolicLink(link, target); } catch { return; } Assert.False(Policy().TryResolve(root, "Linked/game.nsp", out _)); }
    public void Dispose() { try { Directory.Delete(root, true); } catch { } }
}
