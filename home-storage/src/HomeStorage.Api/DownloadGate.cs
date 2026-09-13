namespace HomeStorage;

public sealed class DownloadGate(RuntimeOptions options)
{
    private readonly SemaphoreSlim semaphore = new(options.MaxConcurrentDownloads, options.MaxConcurrentDownloads);
    public async Task<Stream?> OpenAsync(string path, CancellationToken ct)
    {
        if (!await semaphore.WaitAsync(TimeSpan.Zero, ct)) return null;
        try { return new LeaseStream(new FileStream(path, FileMode.Open, FileAccess.Read, FileShare.Read, 128 * 1024, FileOptions.Asynchronous | FileOptions.SequentialScan), semaphore); } catch { semaphore.Release(); throw; }
    }
    private sealed class LeaseStream(Stream inner, SemaphoreSlim lease) : Stream
    {
        private int disposed; public override bool CanRead => inner.CanRead; public override bool CanSeek => inner.CanSeek; public override bool CanWrite => false; public override long Length => inner.Length; public override long Position { get => inner.Position; set => inner.Position = value; }
        public override void Flush() => inner.Flush(); public override int Read(byte[] b, int o, int c) => inner.Read(b, o, c); public override long Seek(long o, SeekOrigin s) => inner.Seek(o, s); public override void SetLength(long v) => throw new NotSupportedException(); public override void Write(byte[] b, int o, int c) => throw new NotSupportedException(); public override ValueTask<int> ReadAsync(Memory<byte> b, CancellationToken ct = default) => inner.ReadAsync(b, ct);
        protected override void Dispose(bool disposing) { if (Interlocked.Exchange(ref disposed, 1) == 0) { if (disposing) inner.Dispose(); lease.Release(); } base.Dispose(disposing); }
        public override async ValueTask DisposeAsync() { if (Interlocked.Exchange(ref disposed, 1) == 0) { await inner.DisposeAsync(); lease.Release(); } GC.SuppressFinalize(this); }
    }
}
