using System.Net;
using System.Net.Sockets;
using System.Text;
using System.Text.Json;
using Microsoft.EntityFrameworkCore;

namespace HomeStorage;

public sealed class DiscoveryService(IDbContextFactory<HomeStorageDb> factory, RuntimeOptions options, ILogger<DiscoveryService> logger) : BackgroundService
{
    private static readonly byte[] Probe = Encoding.ASCII.GetBytes("SWITCHDRIVE_HOME_DISCOVER_V1");
    protected override async Task ExecuteAsync(CancellationToken ct)
    {
        using var udp = new UdpClient(new IPEndPoint(IPAddress.Any, options.DiscoveryPort)); logger.LogInformation("LAN discovery listening on UDP {Port}", options.DiscoveryPort);
        while (!ct.IsCancellationRequested)
        {
            try
            {
                var received = await udp.ReceiveAsync(ct); if (!received.Buffer.AsSpan().SequenceEqual(Probe)) continue;
                await using var db = await factory.CreateDbContextAsync(ct); var settings = await db.Settings.SingleAsync(ct);
                var payload = JsonSerializer.SerializeToUtf8Bytes(new { service = "switch-drive-home-storage", protocolVersion = 1, instanceId = settings.InstanceId, name = settings.InstanceName, httpPort = options.HttpPort, authRequired = settings.AuthRequired });
                await udp.SendAsync(payload, received.RemoteEndPoint, ct);
            }
            catch (OperationCanceledException) when (ct.IsCancellationRequested) { }
            catch (Exception ex) { logger.LogError(ex, "LAN discovery error"); }
        }
    }
}
