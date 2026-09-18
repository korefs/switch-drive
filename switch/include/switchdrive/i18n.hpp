#pragma once

#include <cstddef>
#include <string_view>

namespace switchdrive::i18n {

enum class Language { EnUs, PtBr, EsEs };

// Keep this list in UI/error-flow order. Catalogs in i18n.cpp are required to
// provide exactly one translation for every key.
enum class TextId {
    AppName, Continue,
    InstallNsp, BaseGame, Update, Dlc, TitleId, Version, InstalledVersion,
    SdCard, InternalStorage, Confirm, Cancel, DestinationHint,
    ConnectDrive, ConfigMissing, StartFailed, OpenOnPhone, Code, CheckNow, ScanWithPhone,
    Connected, ConnectionConfirmed, GoToFiles,
    DisconnectAccount, DisconnectAccountConfirm, DisconnectingAccount,
    AccountDisconnected, ConnectAccountFirst, RemoteChanged, PartialDownloadFound,
    BytesConfirmed, RemoteVersionChanged, PartialPreserved, Restart, Resume,
    PartialIdentityMissing, PartialTooLarge, DownloadAlreadyExists,
    ChecksumMismatch, InstallFailed, InstalledCleanupPending,
    RestartDownload, CannotDeletePartial, InvalidPartial, InsufficientSpace,
    FreeSpaceAndRetry, StorageError, ResumingDownload, Transfers, NoActiveTransfers, Downloading,
    InstallAfterDownloadQueued,
    DownloadComplete, DownloadInvalid, Paused, RangeRejected,
    InterruptedDownload, SelectToResume, CompleteAwaitingVerification,
    NspRecovery, NoNspInstallWillStart, Files, DriveError,
    MyDrive, SharedWithMe, EmptyFolder, BrowseHint, Library, NoIndexedDownloads,
    MissingFile,
    Home, Settings, NoAccountConnected, ActiveAccount, OpenFiles,
    NavigationHint, Exit, Language, PairingServiceApi, InvalidPairingServiceUrl,
    NroInvalidHeader, NroInvalidSize, NroExists,
    NspAmbiguous, NspMissingContents, NspTicketCertificateMissing,
    NspTicketImportFailed, CnmtTruncated, CnmtInvalidContents,
    CnmtInvalidBase, CnmtInvalidUpdate, CnmtInvalidDlc, CnmtUnsupported,
    CnmtEmptyContent, CnmtNoInstallableContent, CnmtMissing,
    CnmtSwitchOnly, CnmtTempDirectoryFailed, CnmtPrepareFailed,
    CnmtOpenFailed, CnmtFileMissing, NspContentMissing,
    CnmtDirectoryReadFailed, CnmtFileOpenFailed, CnmtSizeReadFailed,
    CnmtDataReadFailed, CnmtInvalidSize, InstallFailureUnknown,
    NspQuerySwitchOnly, NcmUnavailable, NsUnavailable, InstalledQueryFailed,
    NspInstallSwitchOnly, DowngradeBlocked, MetadataNcaMissing,
    InstallDestinationOpenFailed, DestinationNoSpace, InvalidNcaId,
    NcaQueryFailed, PlaceholderCreateFailed, NcaReserveFailed, NcaMissingDuringInstall,
    NcaWriteFailed, InstallCancelled, NcaRegisterFailed, MetadataCommitFailed,
    ApplicationRecordUpdateFailed, InstallComplete, NonBaseHomeHint,
    DownloadRecordMissing, UnsupportedInstallType,
    JournalUpdateAfterInstallFailed, RecoverySwitchOnly, RecoveryNcmUnavailable,
    RecoveryCommitCheckFailed,
    HttpWriteNotAllowed, ServerRangeNotConfirmed, ResponseSaveFailed,
    InvalidServiceJson, HttpRequestFailed, CurlUnavailable, DownloadPaused, RangeDenied,
    DownloadSizeMismatch, PairingResponseIncomplete, AwaitingAuthorization,
    AccessTokenMissing, SaveFailed, AccountLabel, BrowseDrive, OpenLibrary,
    LibraryItemCount, InstallingBytes, PartialIdentityRestart,
    SdCardInsufficient, SeekFailed, TruncateFailed, LogicalFileOpenFailed,
    InvalidSegmentSize, ConcatenatedCreateFailed, LogicalFileMissing,
    LogicalFileClosed, LogicalFileReadFailed, LogicalFileNotWritable,
    SegmentWriteFailed, LogicalFileWriteFailed, InvalidSegment,
    FileSizeQueryFailed, FileFlushFailed, StateTempOpenFailed, StateWriteFailed,
    JournalInvalid, JournalIncomplete, JournalWriteFailed, JournalSyncFailed,
    JournalCommitFailed, JournalClearCommitFailed, Pfs0InvalidHeader,
    Pfs0InvalidEntries, Pfs0InvalidData, Pfs0InvalidName, Pfs0ReadOutOfBounds,
    NczInvalidHeader, NczInvalidSections, NczInvalidBlock, NczDecompressionFailed,
    RestartCancelHint, ResumeRestartCancelHint,
    TransferCalculating, TransferEstimate,
    SizeBytes, SizeKiB, SizeMiB, SizeGiB,
    DurationSeconds, DurationMinutes, DurationHours,
    GraphicsUnavailable, AppletModeWarning, OperationCancelled,
    ButtonA, ButtonX, ButtonY, Folder, FileSize, HomeSubtitle, FilesSubtitle,
    LibrarySubtitle, SettingsSubtitle, NetworkUnavailable, Ellipsis,
    ControllerReady, ControllerMissing, InputUnfocused, AppVersion,
    PreparingDownload, LoadingFiles, VerifyingDownload, DownloadPauseHint,
    DeleteDownload, DeleteDownloadWarning,
    DownloadRemovalPending, DownloadRemovalUnsafePath,
    StorageProviders, HomeStorage, DetectNetwork, ManualSetup, ServerAddress,
    Username, Password, DetectingStorage, NoStorageFound, InvalidAddress,
    HomeBrowseHint, HideCatalogEntry, HideCatalogConfirm, ProviderConnected,
    ButtonZL, ButtonL, ButtonR, Download, DownloadAndInstall, Back,
    RestartRequired, ExitConfirm, ExitActiveConfirm,
    NetworkProfileDiagnostic, DownloadPerformanceDiagnostic,
    Count
};

Language parseLanguage(std::string_view code);
std::string_view languageCode(Language language);
std::string_view languageName(Language language);
Language nextLanguage(Language language);
void setLanguage(Language language);
Language currentLanguage();
const char* tr(TextId id);
constexpr size_t textCount() { return static_cast<size_t>(TextId::Count); }

} // namespace switchdrive::i18n
