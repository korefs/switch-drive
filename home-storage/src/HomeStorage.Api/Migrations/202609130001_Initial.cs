using Microsoft.EntityFrameworkCore.Infrastructure;
using Microsoft.EntityFrameworkCore.Migrations;

namespace HomeStorage.Migrations;

[DbContext(typeof(HomeStorageDb))]
[Migration("202609130001_Initial")]
public sealed class Initial : Migration
{
    protected override void Up(MigrationBuilder m)
    {
        m.CreateTable("Admins", t => new { Id = t.Column<int>(nullable: false), Username = t.Column<string>(nullable: false), PasswordHash = t.Column<string>(nullable: false) }, constraints: t => t.PrimaryKey("PK_Admins", x => x.Id));
        m.CreateTable("LibraryCredentials", t => new { Id = t.Column<int>(nullable: false), Username = t.Column<string>(nullable: false), PasswordHash = t.Column<string>(nullable: false), AllowCatalogManage = t.Column<bool>(nullable: false), Version = t.Column<int>(nullable: false) }, constraints: t => t.PrimaryKey("PK_LibraryCredentials", x => x.Id));
        m.CreateTable("Settings", t => new { Id = t.Column<int>(nullable: false), InstanceId = t.Column<string>(nullable: false), InstanceName = t.Column<string>(nullable: false), LibraryPath = t.Column<string>(nullable: false), AuthRequired = t.Column<bool>(nullable: false), LastScanAt = t.Column<DateTimeOffset>(nullable: true), LastScanStatus = t.Column<string>(nullable: false), LastScanError = t.Column<string>(nullable: true) }, constraints: t => t.PrimaryKey("PK_Settings", x => x.Id));
        m.CreateTable("DeviceTokens", t => new { Id = t.Column<string>(nullable: false), TokenHash = t.Column<string>(nullable: false), Name = t.Column<string>(nullable: false), CanManageCatalog = t.Column<bool>(nullable: false), CredentialVersion = t.Column<int>(nullable: false), CreatedAt = t.Column<DateTimeOffset>(nullable: false), LastUsedAt = t.Column<DateTimeOffset>(nullable: true), RevokedAt = t.Column<DateTimeOffset>(nullable: true) }, constraints: t => t.PrimaryKey("PK_DeviceTokens", x => x.Id));
        m.CreateTable("Catalog", t => new { Id = t.Column<string>(nullable: false), ParentId = t.Column<string>(nullable: true), Kind = t.Column<string>(nullable: false), RelativePath = t.Column<string>(nullable: false), Name = t.Column<string>(nullable: false), Extension = t.Column<string>(nullable: false), Size = t.Column<long>(nullable: false), ModifiedUtcTicks = t.Column<long>(nullable: false), Sha256 = t.Column<string>(nullable: false), ETag = t.Column<string>(nullable: false), Active = t.Column<bool>(nullable: false), Suppressed = t.Column<bool>(nullable: false), LastSeenScanId = t.Column<string>(nullable: false), TitleId = t.Column<string>(nullable: true), Title = t.Column<string>(nullable: true), Publisher = t.Column<string>(nullable: true), Version = t.Column<long>(nullable: true), ContentType = t.Column<string>(nullable: true), RequiredFirmware = t.Column<long>(nullable: true), RelatedBaseTitleId = t.Column<string>(nullable: true), IconRelativePath = t.Column<string>(nullable: true) }, constraints: t => t.PrimaryKey("PK_Catalog", x => x.Id));
        m.CreateIndex("IX_Catalog_RelativePath", "Catalog", "RelativePath", unique: true); m.CreateIndex("IX_Catalog_ParentId_Active_Suppressed", "Catalog", new[] { "ParentId", "Active", "Suppressed" }); m.CreateIndex("IX_Catalog_Sha256_Size", "Catalog", new[] { "Sha256", "Size" }); m.CreateIndex("IX_DeviceTokens_TokenHash", "DeviceTokens", "TokenHash", unique: true);
    }
    protected override void Down(MigrationBuilder m) { m.DropTable("Admins"); m.DropTable("Catalog"); m.DropTable("DeviceTokens"); m.DropTable("LibraryCredentials"); m.DropTable("Settings"); }
}
