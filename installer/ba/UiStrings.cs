using System.Collections.Generic;
using System.Globalization;

namespace Muffin.Setup
{
    /// <summary>
    /// Installer strings, compiled into the BA assembly (no satellite
    /// resources, so the UX container needs no per-culture payloads) and
    /// covering the same languages as the app's own translations.
    ///
    /// The language is picked once at startup from the OS UI culture and can
    /// be changed at runtime from the title-bar selector. Adding a language
    /// = adding one table and one entry in LanguageCodes/LanguageNames.
    /// </summary>
    internal static class UiStrings
    {
        public static readonly string[] LanguageCodes =
        {
            "en", "zh-CN", "zh-TW", "ja", "ko", "de", "fr", "es", "it", "nl", "pl", "pt-BR", "ru", "tr", "vi",
        };

        /// <summary>Native names for the title-bar selector, aligned with LanguageCodes.</summary>
        public static readonly string[] LanguageNames =
        {
            "English", "简体中文", "繁體中文", "日本語", "한국어", "Deutsch", "Français",
            "Español", "Italiano", "Nederlands", "Polski", "Português (BR)", "Русский",
            "Türkçe", "Tiếng Việt",
        };

        private static readonly Dictionary<string, string> English = new Dictionary<string, string>
        {
            ["LanguageAuto"] = "Auto",
            ["WindowTitle"] = "Muffin Setup",
            ["Detecting"] = "Checking installed versions…",
            ["WelcomeTagline"] = "A quiet place for Markdown. Fast, native, and out of your way.",
            ["Version"] = "Version {0}",
            ["Options"] = "OPTIONS",
            ["AddContextMenu"] = "Add \"Open with Muffin\" to the Explorer context menu",
            ["AssociateFiles"] = "Show Muffin in \"Open with\" for Markdown files",
            ["Install"] = "Install",
            ["Update"] = "Update",
            ["Repair"] = "Repair",
            ["Uninstall"] = "Uninstall",
            ["InstalledTitle"] = "Muffin is installed",
            ["InstalledSub"] = "Version {0} is ready on this computer.",
            ["UninstallTitle"] = "Uninstall Muffin?",
            ["UninstallBody"] = "Muffin will be removed from this computer. Your notes, documents, and personal settings are kept.",
            ["Keep"] = "Keep",
            ["Installing"] = "Installing Muffin…",
            ["Updating"] = "Updating Muffin…",
            ["Repairing"] = "Repairing Muffin…",
            ["Removing"] = "Removing Muffin…",
            ["Cancel"] = "Cancel",
            ["SuccessInstalled"] = "Muffin is ready",
            ["SuccessRemoved"] = "Muffin was removed",
            ["RestartNote"] = "A restart is recommended to finish updating system files.",
            ["Launch"] = "Launch Muffin",
            ["Close"] = "Close",
            ["FailedTitle"] = "Setup could not finish",
            ["OpenLog"] = "Open setup log",
            ["NewerInstalled"] = "A newer version of Muffin is already installed. Uninstall it first if you want to install this version.",
            ["ErrorFmt"] = "Setup could not finish (error 0x{0:X8}). The log has the details.",
            ["PlanErrorFmt"] = "Setup could not be prepared (error 0x{0:X8}).",
        };

        private static readonly Dictionary<string, string> ChineseSimplified = new Dictionary<string, string>
        {
            ["LanguageAuto"] = "自动",
            ["WindowTitle"] = "Muffin 安装程序",
            ["Detecting"] = "正在检查已安装的版本…",
            ["WelcomeTagline"] = "安静的 Markdown 写作空间。快速、原生、不打扰。",
            ["Version"] = "版本 {0}",
            ["Options"] = "选项",
            ["AddContextMenu"] = "在资源管理器右键菜单中添加“用 Muffin 打开”",
            ["AssociateFiles"] = "在 Markdown 文件的“打开方式”中显示 Muffin",
            ["Install"] = "安装",
            ["Update"] = "更新",
            ["Repair"] = "修复",
            ["Uninstall"] = "卸载",
            ["InstalledTitle"] = "Muffin 已安装",
            ["InstalledSub"] = "版本 {0} 已就绪。",
            ["UninstallTitle"] = "要卸载 Muffin 吗？",
            ["UninstallBody"] = "将从这台电脑移除 Muffin。你的笔记、文档和个人设置会保留。",
            ["Keep"] = "保留",
            ["Installing"] = "正在安装 Muffin…",
            ["Updating"] = "正在更新 Muffin…",
            ["Repairing"] = "正在修复 Muffin…",
            ["Removing"] = "正在移除 Muffin…",
            ["Cancel"] = "取消",
            ["SuccessInstalled"] = "Muffin 已就绪",
            ["SuccessRemoved"] = "Muffin 已移除",
            ["RestartNote"] = "建议重启电脑以完成系统文件更新。",
            ["Launch"] = "启动 Muffin",
            ["Close"] = "关闭",
            ["FailedTitle"] = "安装未能完成",
            ["OpenLog"] = "打开安装日志",
            ["NewerInstalled"] = "已安装更高版本的 Muffin。如需安装此版本，请先卸载新版。",
            ["ErrorFmt"] = "安装未能完成（错误 0x{0:X8}）。详细信息请查看日志。",
            ["PlanErrorFmt"] = "无法准备安装（错误 0x{0:X8}）。",
        };

        private static readonly Dictionary<string, string> ChineseTraditional = new Dictionary<string, string>
        {
            ["LanguageAuto"] = "自動",
            ["WindowTitle"] = "Muffin 安裝程式",
            ["Detecting"] = "正在檢查已安裝的版本…",
            ["WelcomeTagline"] = "安靜的 Markdown 寫作空間。快速、原生、不打擾。",
            ["Version"] = "版本 {0}",
            ["Options"] = "選項",
            ["AddContextMenu"] = "在檔案總管右鍵選單中加入「以 Muffin 開啟」",
            ["AssociateFiles"] = "在 Markdown 檔案的「開啟檔案」中顯示 Muffin",
            ["Install"] = "安裝",
            ["Update"] = "更新",
            ["Repair"] = "修復",
            ["Uninstall"] = "解除安裝",
            ["InstalledTitle"] = "Muffin 已安裝",
            ["InstalledSub"] = "版本 {0} 已就緒。",
            ["UninstallTitle"] = "要解除安裝 Muffin 嗎？",
            ["UninstallBody"] = "將從這台電腦移除 Muffin。你的筆記、文件和個人設定會保留。",
            ["Keep"] = "保留",
            ["Installing"] = "正在安裝 Muffin…",
            ["Updating"] = "正在更新 Muffin…",
            ["Repairing"] = "正在修復 Muffin…",
            ["Removing"] = "正在移除 Muffin…",
            ["Cancel"] = "取消",
            ["SuccessInstalled"] = "Muffin 已就緒",
            ["SuccessRemoved"] = "Muffin 已移除",
            ["RestartNote"] = "建議重新開機以完成系統檔案更新。",
            ["Launch"] = "啟動 Muffin",
            ["Close"] = "關閉",
            ["FailedTitle"] = "安裝無法完成",
            ["OpenLog"] = "開啟安裝記錄",
            ["NewerInstalled"] = "已安裝較新版本的 Muffin。如要安裝此版本，請先解除安裝新版。",
            ["ErrorFmt"] = "安裝無法完成（錯誤 0x{0:X8}）。詳細資訊請查看記錄。",
            ["PlanErrorFmt"] = "無法準備安裝（錯誤 0x{0:X8}）。",
        };

        private static readonly Dictionary<string, string> Japanese = new Dictionary<string, string>
        {
            ["LanguageAuto"] = "自動",
            ["WindowTitle"] = "Muffin セットアップ",
            ["Detecting"] = "インストール済みバージョンを確認中…",
            ["WelcomeTagline"] = "静かな Markdown 執筆環境。高速・ネイティブ・邪魔をしない。",
            ["Version"] = "バージョン {0}",
            ["Options"] = "オプション",
            ["AddContextMenu"] = "エクスプローラーの右クリックメニューに「Muffin で開く」を追加",
            ["AssociateFiles"] = "Markdown ファイルの「プログラムから開く」に Muffin を表示",
            ["Install"] = "インストール",
            ["Update"] = "更新",
            ["Repair"] = "修復",
            ["Uninstall"] = "アンインストール",
            ["InstalledTitle"] = "Muffin はインストール済みです",
            ["InstalledSub"] = "バージョン {0} がこのコンピューターで利用できます。",
            ["UninstallTitle"] = "Muffin をアンインストールしますか？",
            ["UninstallBody"] = "このコンピューターから Muffin を削除します。メモ・ドキュメント・個人設定は保持されます。",
            ["Keep"] = "維持",
            ["Installing"] = "Muffin をインストールしています…",
            ["Updating"] = "Muffin を更新しています…",
            ["Repairing"] = "Muffin を修復しています…",
            ["Removing"] = "Muffin を削除しています…",
            ["Cancel"] = "キャンセル",
            ["SuccessInstalled"] = "Muffin の準備ができました",
            ["SuccessRemoved"] = "Muffin を削除しました",
            ["RestartNote"] = "システムファイルの更新を完了するには再起動をおすすめします。",
            ["Launch"] = "Muffin を起動",
            ["Close"] = "閉じる",
            ["FailedTitle"] = "セットアップを完了できませんでした",
            ["OpenLog"] = "セットアップログを開く",
            ["NewerInstalled"] = "より新しいバージョンの Muffin が既にインストールされています。このバージョンをインストールするには先にアンインストールしてください。",
            ["ErrorFmt"] = "セットアップを完了できませんでした（エラー 0x{0:X8}）。詳細はログをご覧ください。",
            ["PlanErrorFmt"] = "セットアップを準備できませんでした（エラー 0x{0:X8}）。",
        };

        private static readonly Dictionary<string, string> Korean = new Dictionary<string, string>
        {
            ["LanguageAuto"] = "자동",
            ["WindowTitle"] = "Muffin 설치",
            ["Detecting"] = "설치된 버전 확인 중…",
            ["WelcomeTagline"] = "조용한 Markdown 작성 공간. 빠르고 네이티브이며 방해하지 않습니다.",
            ["Version"] = "버전 {0}",
            ["Options"] = "옵션",
            ["AddContextMenu"] = "탐색기 상황에 맞는 메뉴에 \"Muffin으로 열기\" 추가",
            ["AssociateFiles"] = "Markdown 파일의 \"연결 프로그램\"에 Muffin 표시",
            ["Install"] = "설치",
            ["Update"] = "업데이트",
            ["Repair"] = "복구",
            ["Uninstall"] = "제거",
            ["InstalledTitle"] = "Muffin이 설치되어 있습니다",
            ["InstalledSub"] = "버전 {0}을(를) 사용할 수 있습니다.",
            ["UninstallTitle"] = "Muffin을 제거할까요?",
            ["UninstallBody"] = "이 컴퓨터에서 Muffin을 제거합니다. 메모, 문서, 개인 설정은 유지됩니다.",
            ["Keep"] = "유지",
            ["Installing"] = "Muffin 설치 중…",
            ["Updating"] = "Muffin 업데이트 중…",
            ["Repairing"] = "Muffin 복구 중…",
            ["Removing"] = "Muffin 제거 중…",
            ["Cancel"] = "취소",
            ["SuccessInstalled"] = "Muffin 준비 완료",
            ["SuccessRemoved"] = "Muffin이 제거되었습니다",
            ["RestartNote"] = "시스템 파일 업데이트를 마치려면 다시 시작하는 것이 좋습니다.",
            ["Launch"] = "Muffin 시작",
            ["Close"] = "닫기",
            ["FailedTitle"] = "설치를 완료할 수 없습니다",
            ["OpenLog"] = "설치 로그 열기",
            ["NewerInstalled"] = "더 최신 버전의 Muffin이 이미 설치되어 있습니다. 이 버전을 설치하려면 먼저 제거하세요.",
            ["ErrorFmt"] = "설치를 완료할 수 없습니다(오류 0x{0:X8}). 자세한 내용은 로그를 참고하세요.",
            ["PlanErrorFmt"] = "설치를 준비할 수 없습니다(오류 0x{0:X8}).",
        };

        private static readonly Dictionary<string, string> German = new Dictionary<string, string>
        {
            ["LanguageAuto"] = "Automatisch",
            ["WindowTitle"] = "Muffin Setup",
            ["Detecting"] = "Installierte Versionen werden geprüft…",
            ["WelcomeTagline"] = "Ein ruhiger Ort für Markdown. Schnell, nativ, unaufdringlich.",
            ["Version"] = "Version {0}",
            ["Options"] = "OPTIONEN",
            ["AddContextMenu"] = "„Mit Muffin öffnen“ zum Explorer-Kontextmenü hinzufügen",
            ["AssociateFiles"] = "Muffin unter „Öffnen mit“ für Markdown-Dateien anzeigen",
            ["Install"] = "Installieren",
            ["Update"] = "Aktualisieren",
            ["Repair"] = "Reparieren",
            ["Uninstall"] = "Deinstallieren",
            ["InstalledTitle"] = "Muffin ist installiert",
            ["InstalledSub"] = "Version {0} ist auf diesem Computer bereit.",
            ["UninstallTitle"] = "Muffin deinstallieren?",
            ["UninstallBody"] = "Muffin wird von diesem Computer entfernt. Deine Notizen, Dokumente und persönlichen Einstellungen bleiben erhalten.",
            ["Keep"] = "Behalten",
            ["Installing"] = "Muffin wird installiert…",
            ["Updating"] = "Muffin wird aktualisiert…",
            ["Repairing"] = "Muffin wird repariert…",
            ["Removing"] = "Muffin wird entfernt…",
            ["Cancel"] = "Abbrechen",
            ["SuccessInstalled"] = "Muffin ist bereit",
            ["SuccessRemoved"] = "Muffin wurde entfernt",
            ["RestartNote"] = "Ein Neustart wird empfohlen, um die Systemdateien zu aktualisieren.",
            ["Launch"] = "Muffin starten",
            ["Close"] = "Schließen",
            ["FailedTitle"] = "Setup konnte nicht abgeschlossen werden",
            ["OpenLog"] = "Setup-Protokoll öffnen",
            ["NewerInstalled"] = "Es ist bereits eine neuere Version von Muffin installiert. Deinstalliere diese zuerst, um diese Version zu installieren.",
            ["ErrorFmt"] = "Setup konnte nicht abgeschlossen werden (Fehler 0x{0:X8}). Details im Protokoll.",
            ["PlanErrorFmt"] = "Setup konnte nicht vorbereitet werden (Fehler 0x{0:X8}).",
        };

        private static readonly Dictionary<string, string> French = new Dictionary<string, string>
        {
            ["LanguageAuto"] = "Auto",
            ["WindowTitle"] = "Installation de Muffin",
            ["Detecting"] = "Vérification des versions installées…",
            ["WelcomeTagline"] = "Un espace paisible pour Markdown. Rapide, natif et discret.",
            ["Version"] = "Version {0}",
            ["Options"] = "OPTIONS",
            ["AddContextMenu"] = "Ajouter « Ouvrir avec Muffin » au menu contextuel de l'Explorateur",
            ["AssociateFiles"] = "Afficher Muffin dans « Ouvrir avec » pour les fichiers Markdown",
            ["Install"] = "Installer",
            ["Update"] = "Mettre à jour",
            ["Repair"] = "Réparer",
            ["Uninstall"] = "Désinstaller",
            ["InstalledTitle"] = "Muffin est installé",
            ["InstalledSub"] = "La version {0} est prête sur cet ordinateur.",
            ["UninstallTitle"] = "Désinstaller Muffin ?",
            ["UninstallBody"] = "Muffin sera supprimé de cet ordinateur. Vos notes, documents et paramètres personnels sont conservés.",
            ["Keep"] = "Conserver",
            ["Installing"] = "Installation de Muffin…",
            ["Updating"] = "Mise à jour de Muffin…",
            ["Repairing"] = "Réparation de Muffin…",
            ["Removing"] = "Suppression de Muffin…",
            ["Cancel"] = "Annuler",
            ["SuccessInstalled"] = "Muffin est prêt",
            ["SuccessRemoved"] = "Muffin a été supprimé",
            ["RestartNote"] = "Un redémarrage est recommandé pour terminer la mise à jour des fichiers système.",
            ["Launch"] = "Lancer Muffin",
            ["Close"] = "Fermer",
            ["FailedTitle"] = "L'installation n'a pas pu aboutir",
            ["OpenLog"] = "Ouvrir le journal d'installation",
            ["NewerInstalled"] = "Une version plus récente de Muffin est déjà installée. Désinstallez-la d'abord pour installer cette version.",
            ["ErrorFmt"] = "L'installation n'a pas pu aboutir (erreur 0x{0:X8}). Les détails sont dans le journal.",
            ["PlanErrorFmt"] = "Impossible de préparer l'installation (erreur 0x{0:X8}).",
        };

        private static readonly Dictionary<string, string> Spanish = new Dictionary<string, string>
        {
            ["LanguageAuto"] = "Auto",
            ["WindowTitle"] = "Instalación de Muffin",
            ["Detecting"] = "Comprobando versiones instaladas…",
            ["WelcomeTagline"] = "Un lugar tranquilo para Markdown. Rápido, nativo y sin molestar.",
            ["Version"] = "Versión {0}",
            ["Options"] = "OPCIONES",
            ["AddContextMenu"] = "Añadir «Abrir con Muffin» al menú contextual del Explorador",
            ["AssociateFiles"] = "Mostrar Muffin en «Abrir con» para archivos Markdown",
            ["Install"] = "Instalar",
            ["Update"] = "Actualizar",
            ["Repair"] = "Reparar",
            ["Uninstall"] = "Desinstalar",
            ["InstalledTitle"] = "Muffin está instalado",
            ["InstalledSub"] = "La versión {0} está lista en este equipo.",
            ["UninstallTitle"] = "¿Desinstalar Muffin?",
            ["UninstallBody"] = "Muffin se eliminará de este equipo. Tus notas, documentos y ajustes personales se conservan.",
            ["Keep"] = "Conservar",
            ["Installing"] = "Instalando Muffin…",
            ["Updating"] = "Actualizando Muffin…",
            ["Repairing"] = "Reparando Muffin…",
            ["Removing"] = "Eliminando Muffin…",
            ["Cancel"] = "Cancelar",
            ["SuccessInstalled"] = "Muffin está listo",
            ["SuccessRemoved"] = "Muffin se ha eliminado",
            ["RestartNote"] = "Se recomienda reiniciar para terminar de actualizar los archivos del sistema.",
            ["Launch"] = "Iniciar Muffin",
            ["Close"] = "Cerrar",
            ["FailedTitle"] = "La instalación no pudo completarse",
            ["OpenLog"] = "Abrir registro de instalación",
            ["NewerInstalled"] = "Ya hay instalada una versión más reciente de Muffin. Desinstálala primero si quieres instalar esta versión.",
            ["ErrorFmt"] = "La instalación no pudo completarse (error 0x{0:X8}). Los detalles están en el registro.",
            ["PlanErrorFmt"] = "No se pudo preparar la instalación (error 0x{0:X8}).",
        };

        private static readonly Dictionary<string, string> Italian = new Dictionary<string, string>
        {
            ["LanguageAuto"] = "Auto",
            ["WindowTitle"] = "Installazione di Muffin",
            ["Detecting"] = "Verifica delle versioni installate…",
            ["WelcomeTagline"] = "Un luogo tranquillo per Markdown. Veloce, nativo e discreto.",
            ["Version"] = "Versione {0}",
            ["Options"] = "OPZIONI",
            ["AddContextMenu"] = "Aggiungi «Apri con Muffin» al menu contestuale di Esplora file",
            ["AssociateFiles"] = "Mostra Muffin in «Apri con» per i file Markdown",
            ["Install"] = "Installa",
            ["Update"] = "Aggiorna",
            ["Repair"] = "Ripara",
            ["Uninstall"] = "Disinstalla",
            ["InstalledTitle"] = "Muffin è installato",
            ["InstalledSub"] = "La versione {0} è pronta su questo computer.",
            ["UninstallTitle"] = "Disinstallare Muffin?",
            ["UninstallBody"] = "Muffin verrà rimosso da questo computer. Note, documenti e impostazioni personali verranno conservati.",
            ["Keep"] = "Mantieni",
            ["Installing"] = "Installazione di Muffin…",
            ["Updating"] = "Aggiornamento di Muffin…",
            ["Repairing"] = "Riparazione di Muffin…",
            ["Removing"] = "Rimozione di Muffin…",
            ["Cancel"] = "Annulla",
            ["SuccessInstalled"] = "Muffin è pronto",
            ["SuccessRemoved"] = "Muffin è stato rimosso",
            ["RestartNote"] = "È consigliabile riavviare per completare l'aggiornamento dei file di sistema.",
            ["Launch"] = "Avvia Muffin",
            ["Close"] = "Chiudi",
            ["FailedTitle"] = "L'installazione non è stata completata",
            ["OpenLog"] = "Apri il registro dell'installazione",
            ["NewerInstalled"] = "È già installata una versione più recente di Muffin. Disinstallala prima per installare questa versione.",
            ["ErrorFmt"] = "L'installazione non è stata completata (errore 0x{0:X8}). I dettagli sono nel registro.",
            ["PlanErrorFmt"] = "Impossibile preparare l'installazione (errore 0x{0:X8}).",
        };

        private static readonly Dictionary<string, string> Dutch = new Dictionary<string, string>
        {
            ["LanguageAuto"] = "Auto",
            ["WindowTitle"] = "Muffin-installatie",
            ["Detecting"] = "Geïnstalleerde versies worden gecontroleerd…",
            ["WelcomeTagline"] = "Een rustige plek voor Markdown. Snel, native en onopvallend.",
            ["Version"] = "Versie {0}",
            ["Options"] = "OPTIES",
            ["AddContextMenu"] = "„Openen met Muffin“ toevoegen aan het contextmenu van Verkenner",
            ["AssociateFiles"] = "Muffin tonen bij „Openen met“ voor Markdown-bestanden",
            ["Install"] = "Installeren",
            ["Update"] = "Bijwerken",
            ["Repair"] = "Herstellen",
            ["Uninstall"] = "Verwijderen",
            ["InstalledTitle"] = "Muffin is geïnstalleerd",
            ["InstalledSub"] = "Versie {0} is klaar voor gebruik op deze computer.",
            ["UninstallTitle"] = "Muffin verwijderen?",
            ["UninstallBody"] = "Muffin wordt van deze computer verwijderd. Je notities, documenten en persoonlijke instellingen blijven bewaard.",
            ["Keep"] = "Behouden",
            ["Installing"] = "Muffin wordt geïnstalleerd…",
            ["Updating"] = "Muffin wordt bijgewerkt…",
            ["Repairing"] = "Muffin wordt hersteld…",
            ["Removing"] = "Muffin wordt verwijderd…",
            ["Cancel"] = "Annuleren",
            ["SuccessInstalled"] = "Muffin is klaar",
            ["SuccessRemoved"] = "Muffin is verwijderd",
            ["RestartNote"] = "Een herstart wordt aanbevolen om de systeembestanden bij te werken.",
            ["Launch"] = "Muffin starten",
            ["Close"] = "Sluiten",
            ["FailedTitle"] = "De installatie is niet voltooid",
            ["OpenLog"] = "Installatielogboek openen",
            ["NewerInstalled"] = "Er is al een nieuwere versie van Muffin geïnstalleerd. Verwijder deze eerst om deze versie te installeren.",
            ["ErrorFmt"] = "De installatie is niet voltooid (fout 0x{0:X8}). Zie het logboek voor details.",
            ["PlanErrorFmt"] = "De installatie kon niet worden voorbereid (fout 0x{0:X8}).",
        };

        private static readonly Dictionary<string, string> Polish = new Dictionary<string, string>
        {
            ["LanguageAuto"] = "Auto",
            ["WindowTitle"] = "Instalator Muffin",
            ["Detecting"] = "Sprawdzanie zainstalowanych wersji…",
            ["WelcomeTagline"] = "Spokojne miejsce dla Markdown. Szybki, natywny i nienachalny.",
            ["Version"] = "Wersja {0}",
            ["Options"] = "OPCJE",
            ["AddContextMenu"] = "Dodaj „Otwórz za pomocą Muffin” do menu kontekstowego Eksploratora",
            ["AssociateFiles"] = "Pokazuj Muffin w „Otwórz za pomocą” dla plików Markdown",
            ["Install"] = "Zainstaluj",
            ["Update"] = "Aktualizuj",
            ["Repair"] = "Napraw",
            ["Uninstall"] = "Odinstaluj",
            ["InstalledTitle"] = "Muffin jest zainstalowany",
            ["InstalledSub"] = "Wersja {0} jest gotowa na tym komputerze.",
            ["UninstallTitle"] = "Odinstalować Muffin?",
            ["UninstallBody"] = "Muffin zostanie usunięty z tego komputera. Twoje notatki, dokumenty i ustawienia osobiste zostaną zachowane.",
            ["Keep"] = "Zachowaj",
            ["Installing"] = "Instalowanie Muffin…",
            ["Updating"] = "Aktualizowanie Muffin…",
            ["Repairing"] = "Naprawianie Muffin…",
            ["Removing"] = "Usuwanie Muffin…",
            ["Cancel"] = "Anuluj",
            ["SuccessInstalled"] = "Muffin jest gotowy",
            ["SuccessRemoved"] = "Muffin został usunięty",
            ["RestartNote"] = "Zalecany jest ponowny rozruch, aby zakończyć aktualizację plików systemowych.",
            ["Launch"] = "Uruchom Muffin",
            ["Close"] = "Zamknij",
            ["FailedTitle"] = "Instalacja nie została ukończona",
            ["OpenLog"] = "Otwórz dziennik instalacji",
            ["NewerInstalled"] = "Nowsza wersja Muffin jest już zainstalowana. Odinstaluj ją najpierw, aby zainstalować tę wersję.",
            ["ErrorFmt"] = "Instalacja nie została ukończona (błąd 0x{0:X8}). Szczegóły w dzienniku.",
            ["PlanErrorFmt"] = "Nie można przygotować instalacji (błąd 0x{0:X8}).",
        };

        private static readonly Dictionary<string, string> PortugueseBrazil = new Dictionary<string, string>
        {
            ["LanguageAuto"] = "Auto",
            ["WindowTitle"] = "Instalação do Muffin",
            ["Detecting"] = "Verificando versões instaladas…",
            ["WelcomeTagline"] = "Um lugar tranquilo para Markdown. Rápido, nativo e discreto.",
            ["Version"] = "Versão {0}",
            ["Options"] = "OPÇÕES",
            ["AddContextMenu"] = "Adicionar \"Abrir com Muffin\" ao menu de contexto do Explorador",
            ["AssociateFiles"] = "Mostrar Muffin em \"Abrir com\" para arquivos Markdown",
            ["Install"] = "Instalar",
            ["Update"] = "Atualizar",
            ["Repair"] = "Reparar",
            ["Uninstall"] = "Desinstalar",
            ["InstalledTitle"] = "Muffin está instalado",
            ["InstalledSub"] = "A versão {0} está pronta neste computador.",
            ["UninstallTitle"] = "Desinstalar o Muffin?",
            ["UninstallBody"] = "O Muffin será removido deste computador. Suas notas, documentos e configurações pessoais serão mantidos.",
            ["Keep"] = "Manter",
            ["Installing"] = "Instalando o Muffin…",
            ["Updating"] = "Atualizando o Muffin…",
            ["Repairing"] = "Reparando o Muffin…",
            ["Removing"] = "Removendo o Muffin…",
            ["Cancel"] = "Cancelar",
            ["SuccessInstalled"] = "Muffin está pronto",
            ["SuccessRemoved"] = "Muffin foi removido",
            ["RestartNote"] = "Recomenda-se reiniciar para concluir a atualização dos arquivos do sistema.",
            ["Launch"] = "Iniciar Muffin",
            ["Close"] = "Fechar",
            ["FailedTitle"] = "A instalação não pôde ser concluída",
            ["OpenLog"] = "Abrir log de instalação",
            ["NewerInstalled"] = "Uma versão mais recente do Muffin já está instalada. Desinstale-a primeiro para instalar esta versão.",
            ["ErrorFmt"] = "A instalação não pôde ser concluída (erro 0x{0:X8}). Detalhes no log.",
            ["PlanErrorFmt"] = "Não foi possível preparar a instalação (erro 0x{0:X8}).",
        };

        private static readonly Dictionary<string, string> Russian = new Dictionary<string, string>
        {
            ["LanguageAuto"] = "Авто",
            ["WindowTitle"] = "Установка Muffin",
            ["Detecting"] = "Проверка установленных версий…",
            ["WelcomeTagline"] = "Спокойное место для Markdown. Быстро, нативно, ненавязчиво.",
            ["Version"] = "Версия {0}",
            ["Options"] = "ПАРАМЕТРЫ",
            ["AddContextMenu"] = "Добавить «Открыть в Muffin» в контекстное меню проводника",
            ["AssociateFiles"] = "Показывать Muffin в «Открыть с помощью» для файлов Markdown",
            ["Install"] = "Установить",
            ["Update"] = "Обновить",
            ["Repair"] = "Восстановить",
            ["Uninstall"] = "Удалить",
            ["InstalledTitle"] = "Muffin установлен",
            ["InstalledSub"] = "Версия {0} готова на этом компьютере.",
            ["UninstallTitle"] = "Удалить Muffin?",
            ["UninstallBody"] = "Muffin будет удалён с этого компьютера. Ваши заметки, документы и личные настройки сохранятся.",
            ["Keep"] = "Оставить",
            ["Installing"] = "Установка Muffin…",
            ["Updating"] = "Обновление Muffin…",
            ["Repairing"] = "Восстановление Muffin…",
            ["Removing"] = "Удаление Muffin…",
            ["Cancel"] = "Отмена",
            ["SuccessInstalled"] = "Muffin готов к работе",
            ["SuccessRemoved"] = "Muffin удалён",
            ["RestartNote"] = "Рекомендуется перезагрузить компьютер, чтобы завершить обновление системных файлов.",
            ["Launch"] = "Запустить Muffin",
            ["Close"] = "Закрыть",
            ["FailedTitle"] = "Не удалось завершить установку",
            ["OpenLog"] = "Открыть журнал установки",
            ["NewerInstalled"] = "Более новая версия Muffin уже установлена. Сначала удалите её, чтобы установить эту версию.",
            ["ErrorFmt"] = "Не удалось завершить установку (ошибка 0x{0:X8}). Подробности — в журнале.",
            ["PlanErrorFmt"] = "Не удалось подготовить установку (ошибка 0x{0:X8}).",
        };

        private static readonly Dictionary<string, string> Turkish = new Dictionary<string, string>
        {
            ["LanguageAuto"] = "Otomatik",
            ["WindowTitle"] = "Muffin Kurulumu",
            ["Detecting"] = "Yüklü sürümler denetleniyor…",
            ["WelcomeTagline"] = "Markdown için sakin bir alan. Hızlı, yerel ve rahatsız etmeyen.",
            ["Version"] = "Sürüm {0}",
            ["Options"] = "SEÇENEKLER",
            ["AddContextMenu"] = "Gezgin sağ tık menüsüne \"Muffin ile aç\" ekle",
            ["AssociateFiles"] = "Markdown dosyaları için \"Birlikte aç\"ta Muffin'i göster",
            ["Install"] = "Yükle",
            ["Update"] = "Güncelle",
            ["Repair"] = "Onar",
            ["Uninstall"] = "Kaldır",
            ["InstalledTitle"] = "Muffin yüklü",
            ["InstalledSub"] = "Sürüm {0} bu bilgisayarda hazır.",
            ["UninstallTitle"] = "Muffin kaldırılsın mı?",
            ["UninstallBody"] = "Muffin bu bilgisayardan kaldırılacak. Notlarınız, belgeleriniz ve kişisel ayarlarınız saklanır.",
            ["Keep"] = "Koru",
            ["Installing"] = "Muffin yükleniyor…",
            ["Updating"] = "Muffin güncelleniyor…",
            ["Repairing"] = "Muffin onarılıyor…",
            ["Removing"] = "Muffin kaldırılıyor…",
            ["Cancel"] = "İptal",
            ["SuccessInstalled"] = "Muffin hazır",
            ["SuccessRemoved"] = "Muffin kaldırıldı",
            ["RestartNote"] = "Sistem dosyalarının güncellenmesini tamamlamak için yeniden başlatma önerilir.",
            ["Launch"] = "Muffin'i başlat",
            ["Close"] = "Kapat",
            ["FailedTitle"] = "Kurulum tamamlanamadı",
            ["OpenLog"] = "Kurulum günlüğünü aç",
            ["NewerInstalled"] = "Daha yeni bir Muffin sürümü zaten yüklü. Bu sürümü yüklemek için önce onu kaldırın.",
            ["ErrorFmt"] = "Kurulum tamamlanamadı (hata 0x{0:X8}). Ayrıntılar günlükte.",
            ["PlanErrorFmt"] = "Kurulum hazırlanamadı (hata 0x{0:X8}).",
        };

        private static readonly Dictionary<string, string> Vietnamese = new Dictionary<string, string>
        {
            ["LanguageAuto"] = "Tự động",
            ["WindowTitle"] = "Cài đặt Muffin",
            ["Detecting"] = "Đang kiểm tra các phiên bản đã cài đặt…",
            ["WelcomeTagline"] = "Một không gian yên tĩnh cho Markdown. Nhanh, gốc và không làm phiền.",
            ["Version"] = "Phiên bản {0}",
            ["Options"] = "TÙY CHỌN",
            ["AddContextMenu"] = "Thêm \"Mở bằng Muffin\" vào menu chuột phải của Explorer",
            ["AssociateFiles"] = "Hiển thị Muffin trong \"Mở bằng\" cho các tệp Markdown",
            ["Install"] = "Cài đặt",
            ["Update"] = "Cập nhật",
            ["Repair"] = "Sửa chữa",
            ["Uninstall"] = "Gỡ cài đặt",
            ["InstalledTitle"] = "Muffin đã được cài đặt",
            ["InstalledSub"] = "Phiên bản {0} đã sẵn sàng trên máy tính này.",
            ["UninstallTitle"] = "Gỡ cài đặt Muffin?",
            ["UninstallBody"] = "Muffin sẽ bị xóa khỏi máy tính này. Ghi chú, tài liệu và cài đặt cá nhân của bạn được giữ lại.",
            ["Keep"] = "Giữ lại",
            ["Installing"] = "Đang cài đặt Muffin…",
            ["Updating"] = "Đang cập nhật Muffin…",
            ["Repairing"] = "Đang sửa chữa Muffin…",
            ["Removing"] = "Đang xóa Muffin…",
            ["Cancel"] = "Hủy",
            ["SuccessInstalled"] = "Muffin đã sẵn sàng",
            ["SuccessRemoved"] = "Đã xóa Muffin",
            ["RestartNote"] = "Nên khởi động lại để hoàn tất cập nhật tệp hệ thống.",
            ["Launch"] = "Khởi động Muffin",
            ["Close"] = "Đóng",
            ["FailedTitle"] = "Không thể hoàn tất cài đặt",
            ["OpenLog"] = "Mở nhật ký cài đặt",
            ["NewerInstalled"] = "Đã cài đặt phiên bản Muffin mới hơn. Hãy gỡ nó trước nếu muốn cài phiên bản này.",
            ["ErrorFmt"] = "Không thể hoàn tất cài đặt (lỗi 0x{0:X8}). Chi tiết trong nhật ký.",
            ["PlanErrorFmt"] = "Không thể chuẩn bị cài đặt (lỗi 0x{0:X8}).",
        };

        private static readonly Dictionary<string, Dictionary<string, string>> Tables =
            new Dictionary<string, Dictionary<string, string>>
        {
            ["en"] = English,
            ["zh-CN"] = ChineseSimplified,
            ["zh-TW"] = ChineseTraditional,
            ["ja"] = Japanese,
            ["ko"] = Korean,
            ["de"] = German,
            ["fr"] = French,
            ["es"] = Spanish,
            ["it"] = Italian,
            ["nl"] = Dutch,
            ["pl"] = Polish,
            ["pt-BR"] = PortugueseBrazil,
            ["ru"] = Russian,
            ["tr"] = Turkish,
            ["vi"] = Vietnamese,
        };

        /// <summary>"auto" (follow system) or an entry of LanguageCodes.</summary>
        private static string _selected = "auto";

        private static Dictionary<string, string> _table = English;

        /// <summary>Resolve the startup language from the OS UI culture.</summary>
        public static void Initialize()
        {
            _selected = "auto";
            Resolve();
        }

        /// <summary>Switch explicitly ("auto" re-resolves against the OS culture).</summary>
        public static void SetLanguage(string code)
        {
            if (!Tables.ContainsKey(code))
            {
                code = "auto";
            }
            _selected = code;
            Resolve();
        }

        private static void Resolve()
        {
            if (_selected != "auto")
            {
                _table = Tables[_selected];
                return;
            }

            var name = CultureInfo.CurrentUICulture.Name;
            if (string.IsNullOrEmpty(name))
            {
                _table = English;
                return;
            }

            // Traditional Chinese (incl. HK/MO) and bare "zh" (mainland default)
            // fall back on opposite sides of the strait.
            if (name.StartsWith("zh-TW") || name.StartsWith("zh-HK") || name.StartsWith("zh-MO") || name.StartsWith("zh-Hant"))
            {
                _table = Tables["zh-TW"];
            }
            else if (name.StartsWith("zh"))
            {
                _table = Tables["zh-CN"];
            }
            else if (name.StartsWith("pt"))
            {
                _table = Tables["pt-BR"];
            }
            else
            {
                var twoLetter = CultureInfo.CurrentUICulture.TwoLetterISOLanguageName;
                _table = Tables.TryGetValue(twoLetter, out var table) ? table : English;
            }
        }

        public static string Get(string key)
        {
            if (_table.TryGetValue(key, out var value))
            {
                return value;
            }
            return English.TryGetValue(key, out var fallback) ? fallback : key;
        }
    }
}
