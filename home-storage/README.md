# Switch Drive Home Storage

Home Storage exposes a read-only host folder as a private Switch Drive provider. It is an independent .NET 10 Minimal API: SQLite stores the catalog and credentials, while file bytes are streamed directly from the bind mount and never pass through the existing pairing service, Worker, or Neon/Postgres.

## Architecture

- The ASP.NET Core process hosts the public API, server-rendered administration panel and UDP discovery responder.
- A background indexer walks the configured container directory at startup, every `SCAN_INTERVAL_MINUTES`, or when requested in the panel.
- SQLite and ASP.NET Core data-protection keys live in the writable `/data` volume; the library mount remains read-only.
- Public clients receive opaque catalog IDs. Relative paths are retained only inside SQLite and are revalidated against the configured root whenever a file is opened.
- The metadata pipeline exposes `IFileMetadataExtractor` for later CNMT/NACP/icon extraction without coupling package parsing to scans or downloads.

## Start with Docker

Docker is the only host dependency. From this directory:

```powershell
Copy-Item .env.example .env
# Edit .env. HOST_LIBRARY_PATH must point at an existing folder.
docker compose up -d
docker compose logs -f home-storage
```

Open `http://localhost:8080/setup`, enter the `SETUP_TOKEN`, and choose the administrator and Switch-library credentials. There are no default credentials. The host path is controlled by `HOST_LIBRARY_PATH`; the panel may select only `/library` or a subdirectory already mounted into the container.

The library is mounted with `read_only: true`. Hiding an entry changes SQLite only and never deletes a host file. Back up the `home-storage-data` Docker volume to preserve IDs, settings and device registrations.

## Configuration

| Variable | Default | Purpose |
|---|---:|---|
| `HOST_LIBRARY_PATH` | required | Host folder bound read-only to `/library`; for example `E:/SwitchDrive`. |
| `LIBRARY_PATH` | `/library` | Initial path inside the container. The panel can later select a descendant. |
| `HOME_STORAGE_PORT` | `8080` | HTTP and advertised LAN port. |
| `DISCOVERY_PORT` | `8080` | UDP broadcast discovery port. |
| `INSTANCE_NAME` | `Home Storage` | Name shown on the Switch. |
| `SCAN_INTERVAL_MINUTES` | `5` | Periodic scan interval. |
| `MAX_CONCURRENT_DOWNLOADS` | `4` | Bound on simultaneously open download streams. |
| `SETUP_TOKEN` | required | One-time bootstrap secret; it is never logged. |
| `CLOUDFLARE_TUNNEL_TOKEN` | unset | Token used only by the optional tunnel profile. |

Supported extensions are `.nro`, `.nsp`, `.nsz`, `.xci`, and `.zip`. New or changed files are hashed with SHA-256 using bounded memory. Symlinks and reparse points are skipped. Missing files become inactive; suppressed files stay hidden until restored by the administrator.

## Authentication

Protected access is the default. The Switch exchanges the configured library username/password once at `POST /api/v1/auth/token` and stores only the revocable bearer token. The administrator password, library password and bearer tokens are never stored in plaintext by the service. Anonymous mode allows listing and downloads but not hiding catalog entries.

Plain HTTP should be used only on a trusted LAN. Always use HTTPS when accessing the service over the Internet.

## Test locally

```sh
curl http://localhost:8080/health
curl http://localhost:8080/drive-health
curl -u switch-user:password -X POST http://localhost:8080/api/v1/auth/token
curl -H "Authorization: Bearer TOKEN" "http://localhost:8080/api/v1/catalog?parentId=root"
curl -I -H "Authorization: Bearer TOKEN" http://localhost:8080/api/v1/files/FILE_ID/content
curl -H "Authorization: Bearer TOKEN" -H "Range: bytes=1048576-2097151" -o part.bin http://localhost:8080/api/v1/files/FILE_ID/content
```

A valid range returns `206 Partial Content`, `Content-Length`, `Content-Range`, `Accept-Ranges`, `ETag`, and `Last-Modified`. Invalid ranges return `416`. Do not modify a file while it is being downloaded; a detected size or timestamp change schedules reindexing and prevents use of stale metadata.

Run tests entirely in Docker:

```sh
docker build --target test -t switch-drive-home-storage-tests .
```

## Cloudflare Tunnel / CGNAT

Create a remotely managed HTTP tunnel in Cloudflare and route its public hostname to `http://home-storage:8080`, then set the token in `.env` and run:

```sh
docker compose --profile tunnel up -d
```

Cloudflare Tunnel is outbound-only, so it works behind CGNAT without exposing an inbound router port. Require Home Storage authentication for a public hostname. Do not place an interactive Cloudflare Access login page in front of the Switch API, and never commit the tunnel token.
If `HOME_STORAGE_PORT` is changed, use that same internal port in the tunnel route.

## Switch integration

The Switch can enter a LAN address or public HTTPS hostname manually. “Detect on network” broadcasts `SWITCHDRIVE_HOME_DISCOVER_V1` over UDP 8080 and validates replies with `/drive-health`. Discovery is LAN-only and can be blocked by client isolation; manual configuration remains available.

The public API is documented in [`openapi.yaml`](openapi.yaml). NSP/NSZ metadata columns and an extractor interface exist for later CNMT/NACP/icon parsing; this release indexes only file metadata.
