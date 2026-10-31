#include "I18n.h"

#include <windows.h>

#include <string>
#include <unordered_map>

namespace afm::app::i18n {

namespace {

constexpr LanguageInfo kLanguages[] = {
    {Language::English, "en", "English"},     {Language::Spanish, "es", "Español"},
    {Language::Portuguese, "pt", "Português"}, {Language::French, "fr", "Français"},
    {Language::German, "de", "Deutsch"},       {Language::Italian, "it", "Italiano"},
    {Language::Russian, "ru", "Русский"},
};
static_assert(std::size(kLanguages) == kLanguageCount);

// English, Spanish, Portuguese, French, German, Italian, Russian. Every translation keeps the
// printf conversions of the English text in the same order (checked by afm_i18n_tests), and
// uses only characters the UI font loads (Latin-1 and Cyrillic).
constexpr Entry kEntries[] = {
    // ---- Tabs, toolbar and status bar ----
    {{"Matrix", "Matriz", "Matriz", "Matrice", "Matrix", "Matrice", "Матрица"}},
    {{"Mixer", "Mezclador", "Mixer", "Mixage", "Mixer", "Mixer", "Микшер"}},
    {{"Devices", "Dispositivos", "Dispositivos", "Périphériques", "Geräte", "Dispositivi", "Устройства"}},
    {{"Settings", "Ajustes", "Configurações", "Paramètres", "Einstellungen", "Impostazioni", "Настройки"}},
    {{"Stop", "Detener", "Parar", "Arrêter", "Stopp", "Ferma", "Стоп"}},
    {{"Start", "Iniciar", "Iniciar", "Démarrer", "Start", "Avvia", "Старт"}},
    {{"%s  |  %.0f Hz  |  %u samples (%.2f ms)",
      "%s  |  %.0f Hz  |  %u muestras (%.2f ms)",
      "%s  |  %.0f Hz  |  %u amostras (%.2f ms)",
      "%s  |  %.0f Hz  |  %u échantillons (%.2f ms)",
      "%s  |  %.0f Hz  |  %u Samples (%.2f ms)",
      "%s  |  %.0f Hz  |  %u campioni (%.2f ms)",
      "%s  |  %.0f Гц  |  %u сэмплов (%.2f мс)"}},
    {{"%s  (stopped)", "%s  (detenido)", "%s  (parado)", "%s  (arrêté)", "%s  (gestoppt)", "%s  (fermo)",
      "%s  (остановлено)"}},
    {{"No driver selected", "Ningún driver seleccionado", "Nenhum driver selecionado", "Aucun pilote sélectionné",
      "Kein Treiber ausgewählt", "Nessun driver selezionato", "Драйвер не выбран"}},
    {{"ASIO client connected (PID %u)", "Cliente ASIO conectado (PID %u)", "Cliente ASIO conectado (PID %u)",
      "Client ASIO connecté (PID %u)", "ASIO-Client verbunden (PID %u)", "Client ASIO collegato (PID %u)",
      "ASIO-клиент подключён (PID %u)"}},
    {{"ASIO client: waiting for Traktor/DAW", "Cliente ASIO: esperando a Traktor/DAW",
      "Cliente ASIO: aguardando o Traktor/DAW", "Client ASIO : en attente de Traktor/DAW",
      "ASIO-Client: wartet auf Traktor/DAW", "Client ASIO: in attesa di Traktor/DAW", "ASIO-клиент: ожидание Traktor/DAW"}},
    {{"Virtual ASIO driver not registered", "Driver ASIO virtual no registrado", "Driver ASIO virtual não registrado",
      "Pilote ASIO virtuel non enregistré", "Virtueller ASIO-Treiber nicht registriert",
      "Driver ASIO virtuale non registrato", "Виртуальный ASIO-драйвер не зарегистрирован"}},
    {{"Engine time per block / block period.\nPeak: %.0f%%   Driver overloads: %llu",
      "Tiempo del motor por bloque / duración del bloque.\nPico: %.0f%%   Sobrecargas del driver: %llu",
      "Tempo do motor por bloco / duração do bloco.\nPico: %.0f%%   Sobrecargas do driver: %llu",
      "Temps du moteur par bloc / durée du bloc.\nCrête : %.0f%%   Surcharges du pilote : %llu",
      "Engine-Zeit pro Block / Blockdauer.\nSpitze: %.0f%%   Treiber-Überlastungen: %llu",
      "Tempo del motore per blocco / durata del blocco.\nPicco: %.0f%%   Sovraccarichi del driver: %llu",
      "Время движка на блок / длительность блока.\nПик: %.0f%%   Перегрузки драйвера: %llu"}},
    {{"%s  |  %u x %u matrix  |  mixer: %s",
      "%s  |  matriz %u x %u  |  mezclador: %s",
      "%s  |  matriz %u x %u  |  mixer: %s",
      "%s  |  matrice %u x %u  |  mixage : %s",
      "%s  |  %u x %u Matrix  |  Mixer: %s",
      "%s  |  matrice %u x %u  |  mixer: %s",
      "%s  |  матрица %u x %u  |  микшер: %s"}},
    {{"Running", "En marcha", "Em execução", "En marche", "Läuft", "In esecuzione", "Работает"}},
    {{"Stopped", "Detenido", "Parado", "Arrêté", "Gestoppt", "Fermo", "Остановлено"}},
    {{"Check for updates", "Buscar actualizaciones", "Procurar atualizações", "Rechercher des mises à jour",
      "Nach Updates suchen", "Cerca aggiornamenti", "Проверять обновления"}},
    {{"Once a day, asks GitHub which release is the latest and shows a notice if it is newer.\nNothing is "
      "downloaded or installed automatically.",
      "Una vez al día pregunta a GitHub cuál es la última versión y muestra un aviso si es más nueva.\nNo se "
      "descarga ni se instala nada automáticamente.",
      "Uma vez por dia pergunta ao GitHub qual é a versão mais recente e mostra um aviso se for mais nova.\nNada é "
      "baixado nem instalado automaticamente.",
      "Une fois par jour, demande à GitHub quelle est la dernière version et affiche un avis si elle est plus "
      "récente.\nRien n'est téléchargé ni installé automatiquement.",
      "Fragt einmal am Tag bei GitHub nach der neuesten Version und zeigt einen Hinweis, wenn sie neuer ist.\nEs "
      "wird nichts automatisch heruntergeladen oder installiert.",
      "Una volta al giorno chiede a GitHub qual è l'ultima versione e mostra un avviso se è più recente.\nNon viene "
      "scaricato né installato nulla automaticamente.",
      "Раз в день спрашивает у GitHub, какая версия последняя, и показывает уведомление, если она новее.\nНичего "
      "не скачивается и не устанавливается автоматически."}},
    {{"Open the project page on GitHub", "Abrir la página del proyecto en GitHub", "Abrir a página do projeto no GitHub",
      "Ouvrir la page du projet sur GitHub", "Projektseite auf GitHub öffnen", "Apri la pagina del progetto su GitHub",
      "Открыть страницу проекта на GitHub"}},
    {{"Support AudioFlow Matrix on Ko-fi", "Apoya AudioFlow Matrix en Ko-fi", "Apoie o AudioFlow Matrix no Ko-fi",
      "Soutenir AudioFlow Matrix sur Ko-fi", "AudioFlow Matrix auf Ko-fi unterstützen",
      "Sostieni AudioFlow Matrix su Ko-fi", "Поддержать AudioFlow Matrix на Ko-fi"}},

    // ---- Matrix and mixer ----
    {{"Start the audio engine to see the routing matrix.", "Inicia el motor de audio para ver la matriz de ruteo.",
      "Inicie o motor de áudio para ver a matriz de roteamento.",
      "Démarrez le moteur audio pour voir la matrice de routage.",
      "Starte die Audio-Engine, um die Routing-Matrix zu sehen.",
      "Avvia il motore audio per vedere la matrice di routing.",
      "Запустите аудиодвижок, чтобы увидеть матрицу маршрутизации."}},
    {{"Click a cell to route or unroute; right-click it for volume, mute and pan. Right-click a name to rename it. "
      "Volumes go from 0 to 100: drag them, or double-click to type.",
      "Haz clic en una celda para rutear o quitar la ruta; clic derecho para volumen, mute y paneo. Clic derecho en un "
      "nombre para renombrarlo. Los volúmenes van de 0 a 100: arrástralos o haz doble clic para escribirlos.",
      "Clique numa célula para rotear ou remover a rota; clique direito para volume, mute e pan. Clique direito num nome "
      "para renomeá-lo. Os volumes vão de 0 a 100: arraste-os ou dê um duplo clique para digitar.",
      "Cliquez sur une cellule pour router ou dérouter ; clic droit pour le volume, le mute et le panoramique. Clic "
      "droit sur un nom pour le renommer. Les volumes vont de 0 à 100 : faites-les glisser ou double-cliquez pour les "
      "saisir.",
      "Klicke auf eine Zelle, um zu routen oder die Route zu entfernen; Rechtsklick für Lautstärke, Mute und Pan. "
      "Rechtsklick auf einen Namen, um ihn umzubenennen. Lautstärken gehen von 0 bis 100: ziehen oder doppelklicken "
      "zum Eintippen.",
      "Clic su una cella per instradare o rimuovere; clic destro per volume, mute e pan. Clic destro su un nome per "
      "rinominarlo. I volumi vanno da 0 a 100: trascinali o fai doppio clic per scriverli.",
      "Нажмите на ячейку, чтобы подключить или отключить маршрут; правый клик: громкость, mute и панорама. Правый клик "
      "по имени, чтобы переименовать. Громкость от 0 до 100: перетащите или дважды щёлкните, чтобы ввести."}},
    {{"%s  ->  %s\n%s, volume %.0f%s", "%s  ->  %s\n%s, volumen %.0f%s", "%s  ->  %s\n%s, volume %.0f%s",
      "%s  ->  %s\n%s, volume %.0f%s", "%s  ->  %s\n%s, Lautstärke %.0f%s", "%s  ->  %s\n%s, volume %.0f%s",
      "%s  ->  %s\n%s, громкость %.0f%s"}},
    {{"Volume", "Volumen", "Volume", "Volume", "Lautstärke", "Volume", "Громкость"}},
    {{"Output volume", "Volumen de salida", "Volume de saída", "Volume de sortie", "Ausgangslautstärke",
      "Volume di uscita", "Громкость выхода"}},
    {{"%s: volume %.0f\nRight-click to rename.", "%s: volumen %.0f\nClic derecho para renombrar.",
      "%s: volume %.0f\nClique direito para renomear.", "%s : volume %.0f\nClic droit pour renommer.",
      "%s: Lautstärke %.0f\nRechtsklick zum Umbenennen.", "%s: volume %.0f\nClic destro per rinominare.",
      "%s: громкость %.0f\nПравый клик, чтобы переименовать."}},
    {{"%s (original name: %s)\nRight-click to rename.", "%s (nombre original: %s)\nClic derecho para renombrar.",
      "%s (nome original: %s)\nClique direito para renomear.", "%s (nom d'origine : %s)\nClic droit pour renommer.",
      "%s (Originalname: %s)\nRechtsklick zum Umbenennen.", "%s (nome originale: %s)\nClic destro per rinominare.",
      "%s (исходное имя: %s)\nПравый клик, чтобы переименовать."}},
    {{"%s\nRight-click to rename.", "%s\nClic derecho para renombrar.", "%s\nClique direito para renomear.",
      "%s\nClic droit pour renommer.", "%s\nRechtsklick zum Umbenennen.", "%s\nClic destro per rinominare.",
      "%s\nПравый клик, чтобы переименовать."}},
    {{"Original name: %s", "Nombre original: %s", "Nome original: %s", "Nom d'origine : %s", "Originalname: %s",
      "Nome originale: %s", "Исходное имя: %s"}},
    {{"Name", "Nombre", "Nome", "Nom", "Name", "Nome", "Имя"}},
    {{"Use the original name", "Usar el nombre original", "Usar o nome original", "Reprendre le nom d'origine",
      "Originalnamen verwenden", "Usa il nome originale", "Вернуть исходное имя"}},
    {{"Inputs  \\  Outputs", "Entradas  \\  Salidas", "Entradas  \\  Saídas", "Entrées  \\  Sorties",
      "Eingänge  \\  Ausgänge", "Ingressi  \\  Uscite", "Входы  \\  Выходы"}},
    {{"routed", "ruteado", "roteado", "routé", "geroutet", "instradato", "подключено"}},
    {{"not routed", "sin ruta", "sem rota", "non routé", "nicht geroutet", "non instradato", "не подключено"}},
    {{", muted", ", silenciado", ", silenciado", ", coupé", ", stumm", ", muto", ", без звука"}},
    {{"Routed", "Ruteado", "Roteado", "Routé", "Geroutet", "Instradato", "Подключено"}},
    {{"Mute", "Silenciar", "Silenciar", "Muet", "Stumm", "Muto", "Без звука"}},
    {{"Pan", "Paneo", "Pan", "Panoramique", "Pan", "Pan", "Панорама"}},
    {{"Reset", "Restablecer", "Redefinir", "Réinitialiser", "Zurücksetzen", "Ripristina", "Сбросить"}},
    {{"Drag to adjust, or double-click and type the gain in dB (-60 = off, up to %+.0f).",
      "Arrastra para ajustar, o haz doble clic y escribe la ganancia en dB (-60 = apagado, hasta %+.0f).",
      "Arraste para ajustar, ou dê um duplo clique e digite o ganho em dB (-60 = desligado, até %+.0f).",
      "Faites glisser pour ajuster, ou double-cliquez et tapez le gain en dB (-60 = coupé, jusqu'à %+.0f).",
      "Ziehen zum Einstellen, oder doppelklicken und den Pegel in dB eintippen (-60 = aus, bis %+.0f).",
      "Trascina per regolare, oppure fai doppio clic e scrivi il guadagno in dB (-60 = spento, fino a %+.0f).",
      "Перетащите для настройки или дважды щёлкните и введите усиление в дБ (-60 = выкл., до %+.0f)."}},
    {{"Double-click: 0 dB", "Doble clic: 0 dB", "Duplo clique: 0 dB", "Double-clic : 0 dB", "Doppelklick: 0 dB",
      "Doppio clic: 0 dB", "Двойной клик: 0 дБ"}},
    {{"MUTE", "MUTE", "MUTE", "MUTE", "MUTE", "MUTE", "MUTE"}},
    {{"Start the audio engine to see the mixer.", "Inicia el motor de audio para ver el mezclador.",
      "Inicie o motor de áudio para ver o mixer.", "Démarrez le moteur audio pour voir la console de mixage.",
      "Starte die Audio-Engine, um den Mixer zu sehen.", "Avvia il motore audio per vedere il mixer.",
      "Запустите аудиодвижок, чтобы увидеть микшер."}},
    {{"INPUTS", "ENTRADAS", "ENTRADAS", "ENTRÉES", "EINGÄNGE", "INGRESSI", "ВХОДЫ"}},
    {{"OUTPUT BUSES", "BUSES DE SALIDA", "BARRAMENTOS DE SAÍDA", "BUS DE SORTIE", "AUSGANGSBUSSE", "BUS DI USCITA",
      "ВЫХОДНЫЕ ШИНЫ"}},

    // ---- Devices: master interface and virtual driver ----
    {{"Master ASIO interface (clock source)", "Interfaz ASIO maestra (fuente de reloj)",
      "Interface ASIO mestre (fonte de clock)", "Interface ASIO maître (source d'horloge)",
      "Master-ASIO-Interface (Taktquelle)", "Interfaccia ASIO master (sorgente di clock)",
      "Главный ASIO-интерфейс (источник тактирования)"}},
    {{"Driver", "Driver", "Driver", "Pilote", "Treiber", "Driver", "Драйвер"}},
    {{"(none)", "(ninguno)", "(nenhum)", "(aucun)", "(keins)", "(nessuno)", "(нет)"}},
    {{"(not connected)", "(no conectado)", "(não conectado)", "(non connecté)", "(nicht verbunden)", "(non collegato)",
      "(не подключено)"}},
    {{"No ASIO drivers found. Install your interface's ASIO driver.",
      "No se encontraron drivers ASIO. Instala el driver ASIO de tu interfaz.",
      "Nenhum driver ASIO encontrado. Instale o driver ASIO da sua interface.",
      "Aucun pilote ASIO trouvé. Installez le pilote ASIO de votre interface.",
      "Keine ASIO-Treiber gefunden. Installiere den ASIO-Treiber deines Interfaces.",
      "Nessun driver ASIO trovato. Installa il driver ASIO della tua interfaccia.",
      "ASIO-драйверы не найдены. Установите ASIO-драйвер вашего интерфейса."}},
    {{"Driver setting (%.0f Hz)", "Ajuste del driver (%.0f Hz)", "Configuração do driver (%.0f Hz)",
      "Réglage du pilote (%.0f Hz)", "Treibereinstellung (%.0f Hz)", "Impostazione del driver (%.0f Hz)",
      "Настройка драйвера (%.0f Гц)"}},
    {{"Driver setting", "Ajuste del driver", "Configuração do driver", "Réglage du pilote", "Treibereinstellung",
      "Impostazione del driver", "Настройка драйвера"}},
    {{"Sample rate", "Frecuencia de muestreo", "Taxa de amostragem", "Fréquence d'échantillonnage", "Abtastrate",
      "Frequenza di campionamento", "Частота дискретизации"}},
    {{"Open driver control panel", "Abrir panel de control del driver", "Abrir painel de controle do driver",
      "Ouvrir le panneau de configuration du pilote", "Treiber-Systemsteuerung öffnen",
      "Apri il pannello di controllo del driver", "Открыть панель управления драйвера"}},
    {{"Buffer size is set in the interface's own control panel.",
      "El tamaño de búfer se ajusta en el panel de control de la propia interfaz.",
      "O tamanho do buffer é definido no painel de controle da própria interface.",
      "La taille du tampon se règle dans le panneau de configuration de l'interface.",
      "Die Puffergröße wird im eigenen Control Panel des Interfaces eingestellt.",
      "La dimensione del buffer si imposta nel pannello di controllo dell'interfaccia.",
      "Размер буфера задаётся в панели управления самого интерфейса."}},
    {{"Virtual ASIO driver for Traktor / DAWs", "Driver ASIO virtual para Traktor / DAWs",
      "Driver ASIO virtual para Traktor / DAWs", "Pilote ASIO virtuel pour Traktor / DAW",
      "Virtueller ASIO-Treiber für Traktor / DAWs", "Driver ASIO virtuale per Traktor / DAW",
      "Виртуальный ASIO-драйвер для Traktor / DAW"}},
    {{"\"AudioFlow Matrix ASIO\" is registered.", "\"AudioFlow Matrix ASIO\" está registrado.",
      "\"AudioFlow Matrix ASIO\" está registrado.", "\"AudioFlow Matrix ASIO\" est enregistré.",
      "\"AudioFlow Matrix ASIO\" ist registriert.", "\"AudioFlow Matrix ASIO\" è registrato.",
      "\"AudioFlow Matrix ASIO\" зарегистрирован."}},
    {{"Unregister", "Quitar registro", "Remover registro", "Désenregistrer", "Registrierung aufheben",
      "Annulla registrazione", "Отменить регистрацию"}},
    {{"The registered driver belongs to another version of AudioFlow Matrix: %s. Update it so Traktor uses this "
      "version's driver.",
      "El driver registrado es de otra versión de AudioFlow Matrix: %s. Actualízalo para que Traktor use el driver "
      "de esta versión.",
      "O driver registrado pertence a outra versão do AudioFlow Matrix: %s. Atualize-o para que o Traktor use o "
      "driver desta versão.",
      "Le pilote enregistré appartient à une autre version d'AudioFlow Matrix : %s. Mettez-le à jour pour que "
      "Traktor utilise le pilote de cette version.",
      "Der registrierte Treiber gehört zu einer anderen Version von AudioFlow Matrix: %s. Aktualisiere ihn, damit "
      "Traktor den Treiber dieser Version verwendet.",
      "Il driver registrato appartiene a un'altra versione di AudioFlow Matrix: %s. Aggiornalo perché Traktor usi "
      "il driver di questa versione.",
      "Зарегистрированный драйвер относится к другой версии AudioFlow Matrix: %s. Обновите его, чтобы Traktor "
      "использовал драйвер этой версии."}},
    {{"(file missing)", "(falta el archivo)", "(arquivo ausente)", "(fichier manquant)", "(Datei fehlt)",
      "(file mancante)", "(файл отсутствует)"}},
    {{"Update driver (administrator)", "Actualizar driver (administrador)", "Atualizar driver (administrador)",
      "Mettre à jour le pilote (administrateur)", "Treiber aktualisieren (Administrator)",
      "Aggiorna driver (amministratore)", "Обновить драйвер (администратор)"}},
    {{"Not registered.", "No registrado.", "Não registrado.", "Non enregistré.", "Nicht registriert.", "Non registrato.",
      "Не зарегистрирован."}},
    {{"Register driver (administrator)", "Registrar driver (administrador)", "Registrar driver (administrador)",
      "Enregistrer le pilote (administrateur)", "Treiber registrieren (Administrator)",
      "Registra driver (amministratore)", "Зарегистрировать драйвер (администратор)"}},
    {{"In Traktor: Preferences > Audio Setup > Audio Device: \"AudioFlow Matrix ASIO\". Start AudioFlow Matrix first.",
      "En Traktor: Preferences > Audio Setup > Audio Device: \"AudioFlow Matrix ASIO\". Abre primero AudioFlow "
      "Matrix.",
      "No Traktor: Preferences > Audio Setup > Audio Device: \"AudioFlow Matrix ASIO\". Abra o AudioFlow Matrix "
      "primeiro.",
      "Dans Traktor : Preferences > Audio Setup > Audio Device : \"AudioFlow Matrix ASIO\". Lancez d'abord "
      "AudioFlow Matrix.",
      "In Traktor: Preferences > Audio Setup > Audio Device: \"AudioFlow Matrix ASIO\". Starte zuerst AudioFlow "
      "Matrix.",
      "In Traktor: Preferences > Audio Setup > Audio Device: \"AudioFlow Matrix ASIO\". Avvia prima AudioFlow "
      "Matrix.",
      "В Traktor: Preferences > Audio Setup > Audio Device: \"AudioFlow Matrix ASIO\". Сначала запустите AudioFlow "
      "Matrix."}},
    {{"Client PID %u  |  safety buffer %u blocks (%.1f ms)  |  late blocks: %llu in the last 10 s, %llu in total",
      "PID del cliente %u  |  búfer de seguridad %u bloques (%.1f ms)  |  bloques tarde: %llu en los últimos 10 s, "
      "%llu en total",
      "PID do cliente %u  |  buffer de segurança %u blocos (%.1f ms)  |  blocos atrasados: %llu nos últimos 10 s, "
      "%llu no total",
      "PID du client %u  |  tampon de sécurité %u blocs (%.1f ms)  |  blocs en retard : %llu ces 10 dernières s, "
      "%llu au total",
      "Client-PID %u  |  Sicherheitspuffer %u Blöcke (%.1f ms)  |  verspätete Blöcke: %llu in den letzten 10 s, "
      "%llu insgesamt",
      "PID del client %u  |  buffer di sicurezza %u blocchi (%.1f ms)  |  blocchi in ritardo: %llu negli ultimi "
      "10 s, %llu in totale",
      "PID клиента %u  |  защитный буфер %u блоков (%.1f мс)  |  опоздавшие блоки: %llu за последние 10 с, %llu "
      "всего"}},
    {{"A late block is a short gap (a click) in the audio coming from Traktor. When one happens, the safety buffer "
      "grows by one block, adding a little latency, so it does not happen again.",
      "Un bloque tarde es un pequeño hueco (un clic) en el audio que llega de Traktor. Cuando ocurre, el búfer de "
      "seguridad crece un bloque, con un poco más de latencia, para que no se repita.",
      "Um bloco atrasado é uma pequena falha (um clique) no áudio que vem do Traktor. Quando acontece, o buffer de "
      "segurança cresce um bloco, com um pouco mais de latência, para que não se repita.",
      "Un bloc en retard est une petite coupure (un clic) dans l'audio venant de Traktor. Quand cela arrive, le "
      "tampon de sécurité grandit d'un bloc, avec un peu plus de latence, pour que cela ne se reproduise pas.",
      "Ein verspäteter Block ist eine kurze Lücke (ein Klick) im Audio von Traktor. Passiert das, wächst der "
      "Sicherheitspuffer um einen Block, mit etwas mehr Latenz, damit es nicht wieder vorkommt.",
      "Un blocco in ritardo è un piccolo vuoto (un clic) nell'audio che arriva da Traktor. Quando succede, il buffer "
      "di sicurezza cresce di un blocco, con un po' più di latenza, perché non si ripeta.",
      "Опоздавший блок: короткий пропуск (щелчок) в звуке из Traktor. Когда это происходит, защитный буфер "
      "увеличивается на один блок (немного больше задержка), чтобы это не повторялось."}},
    {{"The client stopped sending audio. Restart its audio (in Traktor: Preferences > Audio Setup, choose the device "
      "again).",
      "El cliente ha dejado de enviar audio. Reinicia su audio (en Traktor: Preferences > Audio Setup, vuelve a elegir "
      "el dispositivo).",
      "O cliente parou de enviar áudio. Reinicie o áudio dele (no Traktor: Preferences > Audio Setup, escolha o "
      "dispositivo de novo).",
      "Le client n'envoie plus d'audio. Redémarrez son audio (dans Traktor : Preferences > Audio Setup, choisissez à "
      "nouveau le périphérique).",
      "Der Client sendet kein Audio mehr. Starte sein Audio neu (in Traktor: Preferences > Audio Setup, das Gerät "
      "erneut wählen).",
      "Il client ha smesso di inviare audio. Riavvia il suo audio (in Traktor: Preferences > Audio Setup, scegli di "
      "nuovo il dispositivo).",
      "Клиент перестал отправлять звук. Перезапустите его аудио (в Traktor: Preferences > Audio Setup, выберите "
      "устройство заново)."}},

    // ---- Devices: mic, loopback, stream ----
    {{"USB microphone (WASAPI)", "Micrófono USB (WASAPI)", "Microfone USB (WASAPI)", "Microphone USB (WASAPI)",
      "USB-Mikrofon (WASAPI)", "Microfono USB (WASAPI)", "USB-микрофон (WASAPI)"}},
    {{"Enable", "Activar", "Ativar", "Activer", "Aktivieren", "Attiva", "Включить"}},
    {{"Exclusive mode", "Modo exclusivo", "Modo exclusivo", "Mode exclusif", "Exklusivmodus", "Modalità esclusiva",
      "Монопольный режим"}},
    {{"Exclusive: slightly lower latency, but Windows then gives the mic to AudioFlow Matrix only.\nGames, Discord "
      "or the browser can no longer use it. Leave it off unless nothing else needs the mic.",
      "Exclusivo: un poco menos de latencia, pero Windows da el micro solo a AudioFlow Matrix.\nJuegos, Discord o el "
      "navegador ya no pueden usarlo. Déjalo desactivado salvo que nada más necesite el micro.",
      "Exclusivo: um pouco menos de latência, mas o Windows entrega o microfone só ao AudioFlow Matrix.\nJogos, "
      "Discord ou o navegador deixam de poder usá-lo. Deixe desativado, a menos que nada mais precise do microfone.",
      "Exclusif : latence un peu plus faible, mais Windows réserve alors le micro à AudioFlow Matrix.\nLes jeux, "
      "Discord ou le navigateur ne peuvent plus l'utiliser. Laissez-le désactivé sauf si rien d'autre n'a besoin du "
      "micro.",
      "Exklusiv: etwas geringere Latenz, aber Windows überlässt das Mikrofon dann nur AudioFlow Matrix.\nSpiele, "
      "Discord oder der Browser können es nicht mehr nutzen. Lass es aus, solange nichts anderes das Mikrofon "
      "braucht.",
      "Esclusiva: latenza un po' più bassa, ma Windows assegna il microfono solo ad AudioFlow Matrix.\nGiochi, "
      "Discord o il browser non possono più usarlo. Lasciala disattivata a meno che nient'altro abbia bisogno del "
      "microfono.",
      "Монопольный: чуть меньше задержка, но Windows отдаёт микрофон только AudioFlow Matrix.\nИгры, Discord и "
      "браузер больше не смогут его использовать. Не включайте, если микрофон нужен другим программам."}},
    {{"Exclusive mode: other apps (games, Discord, the browser) cannot use this mic while AudioFlow Matrix is "
      "running.",
      "Modo exclusivo: otras apps (juegos, Discord, el navegador) no pueden usar este micro mientras AudioFlow "
      "Matrix esté en marcha.",
      "Modo exclusivo: outros apps (jogos, Discord, o navegador) não podem usar este microfone enquanto o AudioFlow "
      "Matrix estiver em execução.",
      "Mode exclusif : les autres applis (jeux, Discord, navigateur) ne peuvent pas utiliser ce micro tant "
      "qu'AudioFlow Matrix fonctionne.",
      "Exklusivmodus: Andere Apps (Spiele, Discord, der Browser) können dieses Mikrofon nicht nutzen, solange "
      "AudioFlow Matrix läuft.",
      "Modalità esclusiva: le altre app (giochi, Discord, il browser) non possono usare questo microfono mentre "
      "AudioFlow Matrix è in esecuzione.",
      "Монопольный режим: другие программы (игры, Discord, браузер) не могут использовать этот микрофон, пока "
      "работает AudioFlow Matrix."}},
    {{"Capture device", "Dispositivo de captura", "Dispositivo de captura", "Périphérique d'enregistrement",
      "Aufnahmegerät", "Dispositivo di registrazione", "Устройство записи"}},
    {{"%s  |  drift %+.1f ppm  |  %s  |  dropouts %llu",
      "%s  |  deriva %+.1f ppm  |  %s  |  cortes %llu",
      "%s  |  deriva %+.1f ppm  |  %s  |  falhas %llu",
      "%s  |  dérive %+.1f ppm  |  %s  |  coupures %llu",
      "%s  |  Drift %+.1f ppm  |  %s  |  Aussetzer %llu",
      "%s  |  deriva %+.1f ppm  |  %s  |  interruzioni %llu",
      "%s  |  дрейф %+.1f ppm  |  %s  |  пропуски %llu"}},
    {{"locked", "sincronizado", "sincronizado", "verrouillé", "synchron", "agganciato", "синхронизировано"}},
    {{"priming", "preparando", "preparando", "amorçage", "füllt Puffer", "in preparazione", "подготовка"}},
    {{"Device disconnected. Press Apply to reopen.", "Dispositivo desconectado. Pulsa Aplicar para reabrirlo.",
      "Dispositivo desconectado. Clique em Aplicar para reabrir.",
      "Périphérique déconnecté. Cliquez sur Appliquer pour le rouvrir.",
      "Gerät getrennt. Klicke auf Anwenden, um es neu zu öffnen.", "Dispositivo scollegato. Premi Applica per riaprirlo.",
      "Устройство отключено. Нажмите «Применить», чтобы открыть его снова."}},
    {{"Denoise (RNNoise)", "Reducción de ruido (RNNoise)", "Redução de ruído (RNNoise)", "Réduction de bruit (RNNoise)",
      "Rauschunterdrückung (RNNoise)", "Riduzione rumore (RNNoise)", "Шумоподавление (RNNoise)"}},
    {{"Gate", "Gate", "Gate", "Gate", "Gate", "Gate", "Гейт"}},
    {{"Threshold", "Umbral", "Limiar", "Seuil", "Schwelle", "Soglia", "Порог"}},
    {{"Denoise needs the mic at 48000 Hz; it runs at %.0f Hz. Set 48000 Hz in Windows (Sound settings > this mic > "
      "Properties > Advanced) and press Apply and restart audio.",
      "La reducción de ruido necesita el micro a 48000 Hz; va a %.0f Hz. Pon 48000 Hz en Windows (Configuración de "
      "sonido > este micro > Propiedades > Avanzado) y pulsa Aplicar y reiniciar el audio.",
      "A redução de ruído precisa do microfone a 48000 Hz; ele está a %.0f Hz. Defina 48000 Hz no Windows "
      "(Configurações de som > este microfone > Propriedades > Avançado) e clique em Aplicar e reiniciar o áudio.",
      "La réduction de bruit exige le micro à 48000 Hz ; il tourne à %.0f Hz. Réglez 48000 Hz dans Windows "
      "(Paramètres son > ce micro > Propriétés > Avancé) puis cliquez sur Appliquer et redémarrer l'audio.",
      "Die Rauschunterdrückung braucht das Mikrofon mit 48000 Hz; es läuft mit %.0f Hz. Stelle in Windows 48000 Hz "
      "ein (Soundeinstellungen > dieses Mikrofon > Eigenschaften > Erweitert) und klicke auf Anwenden und Audio neu "
      "starten.",
      "La riduzione rumore richiede il microfono a 48000 Hz; ora è a %.0f Hz. Imposta 48000 Hz in Windows "
      "(Impostazioni audio > questo microfono > Proprietà > Avanzate) e premi Applica e riavvia l'audio.",
      "Шумоподавлению нужен микрофон на 48000 Гц; сейчас %.0f Гц. Установите 48000 Гц в Windows (Параметры звука > "
      "этот микрофон > Свойства > Дополнительно) и нажмите «Применить и перезапустить аудио»."}},
    {{"Gate %s (%.0f dB)", "Gate %s (%.0f dB)", "Gate %s (%.0f dB)", "Gate %s (%.0f dB)", "Gate %s (%.0f dB)",
      "Gate %s (%.0f dB)", "Гейт %s (%.0f дБ)"}},
    {{"open", "abierto", "aberto", "ouvert", "offen", "aperto", "открыт"}},
    {{"closed", "cerrado", "fechado", "fermé", "geschlossen", "chiuso", "закрыт"}},
    {{"Gate off", "Gate desactivado", "Gate desativado", "Gate désactivé", "Gate aus", "Gate disattivato",
      "Гейт выключен"}},
    {{"Voice %3.0f%%", "Voz %3.0f%%", "Voz %3.0f%%", "Voix %3.0f%%", "Stimme %3.0f%%", "Voce %3.0f%%",
      "Голос %3.0f%%"}},
    {{"+%.0f ms latency", "+%.0f ms de latencia", "+%.0f ms de latência", "+%.0f ms de latence", "+%.0f ms Latenz",
      "+%.0f ms di latenza", "+%.0f мс задержки"}},
    {{"Windows sound loopback (WASAPI)", "Loopback del sonido de Windows (WASAPI)", "Loopback do som do Windows (WASAPI)",
      "Loopback du son de Windows (WASAPI)", "Loopback des Windows-Sounds (WASAPI)",
      "Loopback dell'audio di Windows (WASAPI)", "Loopback звука Windows (WASAPI)"}},
    {{"Adds an input with whatever Windows plays on the chosen device (games, Discord, the browser).\nOther apps keep "
      "playing there as usual.",
      "Añade una entrada con lo que Windows reproduce en el dispositivo elegido (juegos, Discord, el navegador).\nLas "
      "demás apps siguen sonando ahí como siempre.",
      "Adiciona uma entrada com o que o Windows reproduz no dispositivo escolhido (jogos, Discord, o navegador).\nOs "
      "outros apps continuam tocando nele normalmente.",
      "Ajoute une entrée avec ce que Windows joue sur le périphérique choisi (jeux, Discord, navigateur).\nLes autres "
      "applis continuent d'y jouer comme d'habitude.",
      "Fügt einen Eingang mit allem hinzu, was Windows auf dem gewählten Gerät abspielt (Spiele, Discord, der "
      "Browser).\nAndere Apps spielen dort wie gewohnt weiter.",
      "Aggiunge un ingresso con ciò che Windows riproduce sul dispositivo scelto (giochi, Discord, il browser).\nLe "
      "altre app continuano a suonare lì come sempre.",
      "Добавляет вход со всем, что Windows воспроизводит на выбранном устройстве (игры, Discord, браузер).\nДругие "
      "программы продолжают звучать там как обычно."}},
    {{"Playback device", "Dispositivo de reproducción", "Dispositivo de reprodução", "Périphérique de lecture",
      "Wiedergabegerät", "Dispositivo di riproduzione", "Устройство воспроизведения"}},
    {{"This is also the stream output device: routing Loopback to Stream would feed the sound back into itself.",
      "También es el dispositivo de la salida Stream: rutear Loopback a Stream haría que el sonido vuelva a entrar en "
      "sí mismo.",
      "Este também é o dispositivo da saída Stream: rotear Loopback para Stream faria o som voltar para si mesmo.",
      "C'est aussi le périphérique de la sortie Stream : router Loopback vers Stream renverrait le son sur lui-même.",
      "Das ist auch das Gerät des Stream-Ausgangs: Loopback auf Stream zu routen würde den Sound in sich selbst "
      "zurückführen.",
      "È anche il dispositivo dell'uscita Stream: instradare Loopback verso Stream rimanderebbe il suono su se stesso.",
      "Это же устройство использует выход Stream: маршрут Loopback -> Stream вернёт звук сам в себя."}},
    {{"Stream output for Discord / OBS (WASAPI)", "Salida Stream para Discord / OBS (WASAPI)",
      "Saída Stream para Discord / OBS (WASAPI)", "Sortie Stream pour Discord / OBS (WASAPI)",
      "Stream-Ausgang für Discord / OBS (WASAPI)", "Uscita Stream per Discord / OBS (WASAPI)",
      "Выход Stream для Discord / OBS (WASAPI)"}},
    {{"Render device", "Dispositivo de salida", "Dispositivo de saída", "Périphérique de sortie", "Ausgabegerät",
      "Dispositivo di uscita", "Устройство вывода"}},
    {{"Tip: with a virtual cable (e.g. VB-CABLE) pick \"CABLE Input\" here and \"CABLE Output\" as the microphone in "
      "Discord / OBS.",
      "Consejo: con un cable virtual (p. ej. VB-CABLE) elige aquí \"CABLE Input\" y \"CABLE Output\" como micrófono "
      "en Discord / OBS.",
      "Dica: com um cabo virtual (ex. VB-CABLE) escolha \"CABLE Input\" aqui e \"CABLE Output\" como microfone no "
      "Discord / OBS.",
      "Astuce : avec un câble virtuel (ex. VB-CABLE), choisissez \"CABLE Input\" ici et \"CABLE Output\" comme micro "
      "dans Discord / OBS.",
      "Tipp: Mit einem virtuellen Kabel (z. B. VB-CABLE) hier \"CABLE Input\" wählen und \"CABLE Output\" als "
      "Mikrofon in Discord / OBS.",
      "Suggerimento: con un cavo virtuale (es. VB-CABLE) scegli qui \"CABLE Input\" e \"CABLE Output\" come "
      "microfono in Discord / OBS.",
      "Совет: с виртуальным кабелем (например, VB-CABLE) выберите здесь \"CABLE Input\", а в Discord / OBS "
      "микрофоном \"CABLE Output\"."}},
    {{"General", "General", "Geral", "Général", "Allgemein", "Generale", "Общие"}},
    {{"Start audio when AudioFlow Matrix opens", "Iniciar el audio al abrir AudioFlow Matrix",
      "Iniciar o áudio ao abrir o AudioFlow Matrix", "Démarrer l'audio à l'ouverture d'AudioFlow Matrix",
      "Audio beim Öffnen von AudioFlow Matrix starten", "Avvia l'audio all'apertura di AudioFlow Matrix",
      "Запускать аудио при открытии AudioFlow Matrix"}},
    {{"Apply and restart audio", "Aplicar y reiniciar el audio", "Aplicar e reiniciar o áudio",
      "Appliquer et redémarrer l'audio", "Anwenden und Audio neu starten", "Applica e riavvia l'audio",
      "Применить и перезапустить аудио"}},
    {{"Refresh device lists", "Actualizar listas de dispositivos", "Atualizar listas de dispositivos",
      "Actualiser les listes de périphériques", "Gerätelisten aktualisieren", "Aggiorna elenchi dispositivi",
      "Обновить списки устройств"}},
    {{"Clear all routes", "Borrar todas las rutas", "Apagar todas as rotas", "Effacer toutes les routes",
      "Alle Routen löschen", "Cancella tutte le rotte", "Удалить все маршруты"}},

    // ---- Push-to-talk ----
    {{"Push-to-talk", "Push-to-talk", "Push-to-talk", "Push-to-talk", "Push-to-Talk", "Push-to-talk", "Push-to-talk"}},
    {{"ON AIR", "EN EL AIRE", "NO AR", "EN DIRECT", "ON AIR", "IN ONDA", "В ЭФИРЕ"}},
    {{"PTT  (mic muted on stream)", "PTT  (micro silenciado)", "PTT  (micro silenciado)", "PTT  (micro coupé)",
      "PTT  (Mikro stumm)", "PTT  (micro muto)", "PTT  (микрофон выкл.)"}},
    {{"Push-to-talk, mic -> stream (%s)\nMIDI: %s (map it in the MIDI tab)",
      "Push-to-talk, micro -> stream (%s)\nMIDI: %s (asígnalo en la pestaña MIDI)",
      "Push-to-talk, microfone -> stream (%s)\nMIDI: %s (mapeie na aba MIDI)",
      "Push-to-talk, micro -> stream (%s)\nMIDI : %s (à assigner dans l'onglet MIDI)",
      "Push-to-Talk, Mikro -> Stream (%s)\nMIDI: %s (im MIDI-Tab zuweisen)",
      "Push-to-talk, microfono -> stream (%s)\nMIDI: %s (assegnalo nella scheda MIDI)",
      "Push-to-talk, микрофон -> stream (%s)\nMIDI: %s (назначается на вкладке MIDI)"}},
    {{"click to toggle", "clic para alternar", "clique para alternar", "clic pour basculer", "Klick zum Umschalten",
      "clic per alternare", "клик переключает"}},
    {{"hold to talk", "mantener para hablar", "segure para falar", "maintenir pour parler", "halten zum Sprechen",
      "tieni premuto per parlare", "удерживать для разговора"}},
    {{"Push-to-talk: mic -> stream (optional)", "Push-to-talk: micro -> stream (opcional)",
      "Push-to-talk: microfone -> stream (opcional)", "Push-to-talk : micro -> stream (facultatif)",
      "Push-to-Talk: Mikro -> Stream (optional)", "Push-to-talk: microfono -> stream (opzionale)",
      "Push-to-talk: микрофон -> stream (необязательно)"}},
    {{"Enable push-to-talk", "Activar push-to-talk", "Ativar push-to-talk", "Activer le push-to-talk",
      "Push-to-Talk aktivieren", "Attiva push-to-talk", "Включить push-to-talk"}},
    {{"Hold to talk", "Mantener para hablar", "Segurar para falar", "Maintenir pour parler", "Halten zum Sprechen",
      "Tieni premuto per parlare", "Удерживать"}},
    {{"Toggle", "Alternar", "Alternar", "Basculer", "Umschalten", "Alterna", "Переключать"}},
    {{"While enabled, the mic only reaches the Stream output when PTT is open. The mic to your interface and "
      "everything else is unaffected.",
      "Mientras está activado, el micro solo llega a la salida Stream con el PTT abierto. El micro hacia tu interfaz "
      "y todo lo demás no cambia.",
      "Enquanto ativado, o microfone só chega à saída Stream com o PTT aberto. O microfone para a sua interface e "
      "todo o resto não são afetados.",
      "Lorsqu'il est activé, le micro n'atteint la sortie Stream que si le PTT est ouvert. Le micro vers votre "
      "interface et tout le reste ne changent pas.",
      "Wenn aktiv, erreicht das Mikro den Stream-Ausgang nur bei offenem PTT. Das Mikro zu deinem Interface und alles "
      "andere bleiben unverändert.",
      "Quando è attivo, il microfono arriva all'uscita Stream solo con il PTT aperto. Il microfono verso la tua "
      "interfaccia e tutto il resto non cambiano.",
      "Когда включено, микрофон попадает на выход Stream только при открытом PTT. Микрофон на ваш интерфейс и всё "
      "остальное не меняются."}},
    {{"MIDI: %s. Map it, and channel mutes, in the MIDI tab.",
      "MIDI: %s. Asígnalo, igual que los mutes de los canales, en la pestaña MIDI.",
      "MIDI: %s. Mapeie-o, e os mutes dos canais, na aba MIDI.",
      "MIDI : %s. Assignez-le, ainsi que les mutes des canaux, dans l'onglet MIDI.",
      "MIDI: %s. Weise ihn und die Kanal-Mutes im MIDI-Tab zu.",
      "MIDI: %s. Assegnalo, insieme ai mute dei canali, nella scheda MIDI.",
      "MIDI: %s. Назначьте его и mute каналов на вкладке MIDI."}},
    {{"Enable the USB mic and the stream output for PTT to act.",
      "Activa el micro USB y la salida Stream para que el PTT actúe.",
      "Ative o microfone USB e a saída Stream para o PTT funcionar.",
      "Activez le micro USB et la sortie Stream pour que le PTT agisse.",
      "Aktiviere das USB-Mikro und den Stream-Ausgang, damit PTT wirkt.",
      "Attiva il microfono USB e l'uscita Stream perché il PTT funzioni.",
      "Включите USB-микрофон и выход Stream, чтобы PTT работал."}},

    // ---- MIDI tab ----
    {{"MIDI input", "Entrada MIDI", "Entrada MIDI", "Entrée MIDI", "MIDI-Eingang", "Ingresso MIDI", "MIDI-вход"}},
    {{"Refresh", "Actualizar", "Atualizar", "Actualiser", "Aktualisieren", "Aggiorna", "Обновить"}},
    {{"Look for MIDI inputs again and reconnect the selected one.",
      "Busca de nuevo entradas MIDI y reconecta la seleccionada.",
      "Procura entradas MIDI novamente e reconecta a selecionada.",
      "Recherche à nouveau les entrées MIDI et reconnecte celle sélectionnée.",
      "Sucht erneut nach MIDI-Eingängen und verbindet den gewählten neu.",
      "Cerca di nuovo gli ingressi MIDI e ricollega quello selezionato.",
      "Заново искать MIDI-входы и переподключить выбранный."}},
    {{"No MIDI inputs found: connect one and press Refresh.", "No hay entradas MIDI: conecta una y pulsa Actualizar.",
      "Nenhuma entrada MIDI encontrada: conecte uma e clique em Atualizar.",
      "Aucune entrée MIDI trouvée : branchez-en une et cliquez sur Actualiser.",
      "Keine MIDI-Eingänge gefunden: Schließe einen an und klicke auf Aktualisieren.",
      "Nessun ingresso MIDI trovato: collegane uno e premi Aggiorna.",
      "MIDI-входы не найдены: подключите устройство и нажмите «Обновить»."}},
    {{"Listening to \"%s\".", "Escuchando \"%s\".", "Ouvindo \"%s\".", "À l'écoute de \"%s\".", "Empfange von \"%s\".",
      "In ascolto di \"%s\".", "Прослушивается \"%s\"."}},
    {{"Mappable controls", "Controles asignables", "Controles mapeáveis", "Commandes assignables",
      "Zuweisbare Steuerungen", "Controlli assegnabili", "Назначаемые элементы"}},
    {{"Press the pad, key or button for \"%s\"...", "Pulsa el pad, tecla o botón para \"%s\"...",
      "Pressione o pad, tecla ou botão para \"%s\"...", "Appuyez sur le pad, la touche ou le bouton pour \"%s\"...",
      "Drücke das Pad, die Taste oder den Knopf für \"%s\"...", "Premi il pad, il tasto o il pulsante per \"%s\"...",
      "Нажмите пэд, клавишу или кнопку для \"%s\"..."}},
    {{"Pick a MIDI input above to use MIDI Learn.", "Elige una entrada MIDI arriba para usar MIDI Learn.",
      "Escolha uma entrada MIDI acima para usar o MIDI Learn.",
      "Choisissez une entrée MIDI ci-dessus pour utiliser MIDI Learn.",
      "Wähle oben einen MIDI-Eingang, um MIDI Learn zu nutzen.",
      "Scegli un ingresso MIDI qui sopra per usare MIDI Learn.",
      "Выберите MIDI-вход выше, чтобы использовать MIDI Learn."}},
    {{"Press Learn on a control, then the pad, key or button that should drive it.",
      "Pulsa Aprender en un control y luego el pad, tecla o botón que deba manejarlo.",
      "Clique em Aprender num controle e depois no pad, tecla ou botão que deve acioná-lo.",
      "Cliquez sur Apprendre pour une commande, puis sur le pad, la touche ou le bouton qui doit la piloter.",
      "Klicke bei einer Steuerung auf Lernen und dann auf das Pad, die Taste oder den Knopf, der sie steuern soll.",
      "Premi Impara su un controllo, poi il pad, il tasto o il pulsante che deve comandarlo.",
      "Нажмите «Обучить» у элемента, затем пэд, клавишу или кнопку, которая будет им управлять."}},
    {{"Push-to-talk (mic -> stream)", "Push-to-talk (micro -> stream)", "Push-to-talk (microfone -> stream)",
      "Push-to-talk (micro -> stream)", "Push-to-Talk (Mikro -> Stream)", "Push-to-talk (microfono -> stream)",
      "Push-to-talk (микрофон -> stream)"}},
    {{"Mute: %s (input)", "Mute: %s (entrada)", "Mute: %s (entrada)", "Mute : %s (entrée)", "Mute: %s (Eingang)",
      "Mute: %s (ingresso)", "Mute: %s (вход)"}},
    {{"Mute: %s (output)", "Mute: %s (salida)", "Mute: %s (saída)", "Mute : %s (sortie)", "Mute: %s (Ausgang)",
      "Mute: %s (uscita)", "Mute: %s (выход)"}},
    {{"not mapped", "sin asignar", "não mapeado", "non assigné", "nicht zugewiesen", "non assegnato", "не назначено"}},
    {{"Filter", "Filtrar", "Filtrar", "Filtrer", "Filter", "Filtra", "Фильтр"}},
    {{"Start audio to list the channel mutes.", "Inicia el audio para listar los mutes de los canales.",
      "Inicie o áudio para listar os mutes dos canais.", "Démarrez l'audio pour lister les mutes des canaux.",
      "Starte das Audio, um die Kanal-Mutes aufzulisten.", "Avvia l'audio per elencare i mute dei canali.",
      "Запустите аудио, чтобы увидеть mute каналов."}},
    {{"Control", "Control", "Controle", "Commande", "Steuerung", "Controllo", "Элемент"}},
    {{"MIDI control", "Control MIDI", "Controle MIDI", "Commande MIDI", "MIDI-Steuerung", "Controllo MIDI",
      "MIDI-контрол"}},
    {{"%s  (not active)", "%s  (no activo)", "%s  (inativo)", "%s  (inactif)", "%s  (nicht aktiv)", "%s  (non attivo)",
      "%s  (неактивно)"}},
    {{"waiting...", "esperando...", "aguardando...", "en attente...", "warte...", "in attesa...", "ожидание..."}},
    {{"Cancel", "Cancelar", "Cancelar", "Annuler", "Abbrechen", "Annulla", "Отмена"}},
    {{"Learn", "Aprender", "Aprender", "Apprendre", "Lernen", "Impara", "Обучить"}},
    {{"Clear", "Borrar", "Limpar", "Effacer", "Löschen", "Cancella", "Очистить"}},
    {{"Last MIDI messages:", "Últimos mensajes MIDI:", "Últimas mensagens MIDI:", "Derniers messages MIDI :",
      "Letzte MIDI-Nachrichten:", "Ultimi messaggi MIDI:", "Последние MIDI-сообщения:"}},
    {{"Last MIDI messages (no input open):", "Últimos mensajes MIDI (ninguna entrada abierta):",
      "Últimas mensagens MIDI (nenhuma entrada aberta):", "Derniers messages MIDI (aucune entrée ouverte) :",
      "Letzte MIDI-Nachrichten (kein Eingang offen):", "Ultimi messaggi MIDI (nessun ingresso aperto):",
      "Последние MIDI-сообщения (вход не открыт):"}},
    {{"Nothing received yet.", "Aún no se ha recibido nada.", "Nada recebido ainda.", "Rien reçu pour l'instant.",
      "Noch nichts empfangen.", "Ancora niente ricevuto.", "Пока ничего не получено."}},
    {{"Diagnostics log (send it if MIDI misbehaves): %s", "Log de diagnóstico (envíalo si el MIDI falla): %s",
      "Log de diagnóstico (envie se o MIDI falhar): %s",
      "Journal de diagnostic (envoyez-le si le MIDI pose problème) : %s",
      "Diagnoseprotokoll (schicke es, wenn MIDI Probleme macht): %s",
      "Log di diagnostica (invialo se il MIDI dà problemi): %s",
      "Журнал диагностики (пришлите его, если MIDI работает неправильно): %s"}},
    {{"Open log folder", "Abrir carpeta de logs", "Abrir pasta de logs", "Ouvrir le dossier des journaux",
      "Protokollordner öffnen", "Apri la cartella dei log", "Открыть папку журналов"}},

    // ---- Settings tab and update notice ----
    {{"Language", "Idioma", "Idioma", "Langue", "Sprache", "Lingua", "Язык"}},
    {{"Messages that come from Windows or the audio drivers, and the diagnostics log, stay in English.",
      "Los mensajes que vienen de Windows o de los drivers de audio, y el log de diagnóstico, siguen en inglés.",
      "As mensagens vindas do Windows ou dos drivers de áudio, e o log de diagnóstico, continuam em inglês.",
      "Les messages venant de Windows ou des pilotes audio, ainsi que le journal de diagnostic, restent en anglais.",
      "Meldungen von Windows oder den Audiotreibern sowie das Diagnoseprotokoll bleiben auf Englisch.",
      "I messaggi di Windows o dei driver audio, e il log di diagnostica, restano in inglese.",
      "Сообщения от Windows и аудиодрайверов, а также журнал диагностики остаются на английском."}},
    {{"Window", "Ventana", "Janela", "Fenêtre", "Fenster", "Finestra", "Окно"}},
    {{"Minimize to the system tray", "Minimizar a la bandeja del sistema", "Minimizar para a bandeja do sistema",
      "Réduire dans la zone de notification", "In den Infobereich minimieren", "Riduci nell'area di notifica",
      "Сворачивать в системный трей"}},
    {{"When minimized, AudioFlow Matrix leaves the taskbar and stays as an icon next to the clock, with audio and MIDI "
      "still running. Click the icon to open it again; right-click it to exit.",
      "Al minimizar, AudioFlow Matrix sale de la barra de tareas y queda como un icono junto al reloj, con el audio y "
      "el MIDI funcionando. Haz clic en el icono para abrirlo de nuevo; clic derecho para salir.",
      "Ao minimizar, o AudioFlow Matrix sai da barra de tarefas e fica como um ícone ao lado do relógio, com o áudio e "
      "o MIDI funcionando. Clique no ícone para abri-lo de novo; clique direito para sair.",
      "Une fois réduit, AudioFlow Matrix quitte la barre des tâches et reste en icône près de l'horloge, l'audio et le "
      "MIDI continuant de fonctionner. Cliquez sur l'icône pour le rouvrir ; clic droit pour quitter.",
      "Minimiert verschwindet AudioFlow Matrix aus der Taskleiste und bleibt als Symbol neben der Uhr, Audio und MIDI "
      "laufen weiter. Klicke auf das Symbol, um es wieder zu öffnen; Rechtsklick zum Beenden.",
      "Quando è ridotto, AudioFlow Matrix esce dalla barra delle applicazioni e resta come icona vicino all'orologio, "
      "con audio e MIDI attivi. Fai clic sull'icona per riaprirlo; clic destro per uscire.",
      "При сворачивании AudioFlow Matrix исчезает с панели задач и остаётся значком возле часов, а аудио и MIDI "
      "продолжают работать. Нажмите на значок, чтобы открыть окно; правый клик, чтобы выйти."}},
    {{"Interface smoothness", "Fluidez de la interfaz", "Fluidez da interface", "Fluidité de l'interface",
      "Flüssigkeit der Oberfläche", "Fluidità dell'interfaccia", "Плавность интерфейса"}},
    {{"15 fps (lowest CPU use)", "15 fps (mínimo uso de CPU)", "15 fps (menor uso de CPU)",
      "15 fps (CPU minimale)", "15 fps (geringste CPU-Last)", "15 fps (uso minimo della CPU)",
      "15 fps (минимальная нагрузка на ЦП)"}},
    {{"30 fps", "30 fps", "30 fps", "30 fps", "30 fps", "30 fps", "30 fps"}},
    {{"60 fps (recommended)", "60 fps (recomendado)", "60 fps (recomendado)", "60 fps (recommandé)",
      "60 fps (empfohlen)", "60 fps (consigliato)", "60 fps (рекомендуется)"}},
    {{"Match the display (%u Hz)", "Igual que la pantalla (%u Hz)", "Igual à tela (%u Hz)", "Comme l'écran (%u Hz)",
      "Wie der Bildschirm (%u Hz)", "Come lo schermo (%u Hz)", "Как у экрана (%u Гц)"}},
    {{"Fewer frames per second use less CPU; the meters move less smoothly. Audio is not affected.",
      "Menos fotogramas por segundo gastan menos CPU; los medidores se mueven menos suaves. El audio no se ve "
      "afectado.",
      "Menos quadros por segundo gastam menos CPU; os medidores se movem com menos suavidade. O áudio não é afetado.",
      "Moins d'images par seconde consomment moins de CPU ; les vumètres bougent moins fluidement. L'audio n'est pas "
      "affecté.",
      "Weniger Bilder pro Sekunde brauchen weniger CPU; die Pegelanzeigen bewegen sich weniger flüssig. Das Audio ist "
      "nicht betroffen.",
      "Meno fotogrammi al secondo usano meno CPU; i misuratori si muovono meno fluidi. L'audio non è influenzato.",
      "Чем меньше кадров в секунду, тем ниже нагрузка на ЦП; индикаторы двигаются менее плавно. На звук это не "
      "влияет."}},
    {{"Slow down to 15 fps while the window is in the background",
      "Bajar a 15 fps cuando la ventana está en segundo plano", "Baixar para 15 fps quando a janela está em segundo plano",
      "Passer à 15 fps quand la fenêtre est en arrière-plan", "Im Hintergrund auf 15 fps verlangsamen",
      "Scendere a 15 fps quando la finestra è in secondo piano", "Снижать до 15 fps, когда окно в фоне"}},
    {{"Open AudioFlow Matrix", "Abrir AudioFlow Matrix", "Abrir o AudioFlow Matrix", "Ouvrir AudioFlow Matrix",
      "AudioFlow Matrix öffnen", "Apri AudioFlow Matrix", "Открыть AudioFlow Matrix"}},
    {{"Exit", "Salir", "Sair", "Quitter", "Beenden", "Esci", "Выход"}},
    {{"Channel names", "Nombres de los canales", "Nomes dos canais", "Noms des canaux", "Kanalnamen", "Nomi dei canali",
      "Имена каналов"}},
    {{"Restore all channel names", "Restaurar todos los nombres", "Restaurar todos os nomes", "Rétablir tous les noms",
      "Alle Namen zurücksetzen", "Ripristina tutti i nomi", "Вернуть все исходные имена"}},
    {{"%zu renamed. To rename one, right-click its name in the Matrix or the Mixer.",
      "%zu renombrados. Para renombrar uno, clic derecho en su nombre en la Matriz o el Mezclador.",
      "%zu renomeados. Para renomear um, clique direito no nome dele na Matriz ou no Mixer.",
      "%zu renommés. Pour en renommer un, clic droit sur son nom dans la Matrice ou le Mixage.",
      "%zu umbenannt. Zum Umbenennen Rechtsklick auf den Namen in der Matrix oder im Mixer.",
      "%zu rinominati. Per rinominarne uno, clic destro sul nome nella Matrice o nel Mixer.",
      "Переименовано: %zu. Чтобы переименовать, щёлкните правой кнопкой по имени в матрице или микшере."}},
    {{"Updates", "Actualizaciones", "Atualizações", "Mises à jour", "Updates", "Aggiornamenti", "Обновления"}},
    {{"Check now", "Comprobar ahora", "Verificar agora", "Vérifier maintenant", "Jetzt prüfen", "Controlla ora",
      "Проверить сейчас"}},
    {{"Checking...", "Comprobando...", "Verificando...", "Vérification...", "Wird geprüft...", "Controllo in corso...",
      "Проверка..."}},
    {{"Could not check: %s", "No se pudo comprobar: %s", "Não foi possível verificar: %s", "Vérification impossible : %s",
      "Prüfung fehlgeschlagen: %s", "Impossibile controllare: %s", "Не удалось проверить: %s"}},
    {{"Not checked yet.", "Aún no se ha comprobado.", "Ainda não verificado.", "Pas encore vérifié.",
      "Noch nicht geprüft.", "Non ancora controllato.", "Ещё не проверялось."}},
    {{"New version available: %s", "Nueva versión disponible: %s", "Nova versão disponível: %s",
      "Nouvelle version disponible : %s", "Neue Version verfügbar: %s", "Nuova versione disponibile: %s",
      "Доступна новая версия: %s"}},
    {{"Up to date (latest release: %s).", "Al día (última versión: %s).", "Atualizado (última versão: %s).",
      "À jour (dernière version : %s).", "Aktuell (neueste Version: %s).", "Aggiornato (ultima versione: %s).",
      "Версия актуальна (последняя: %s)."}},
    {{"This version: %s", "Esta versión: %s", "Esta versão: %s", "Cette version : %s", "Diese Version: %s",
      "Questa versione: %s", "Эта версия: %s"}},
    {{"A new version of AudioFlow Matrix is available: %s (you have %s).",
      "Hay una nueva versión de AudioFlow Matrix disponible: %s (tienes la %s).",
      "Há uma nova versão do AudioFlow Matrix disponível: %s (você tem a %s).",
      "Une nouvelle version d'AudioFlow Matrix est disponible : %s (vous avez la %s).",
      "Eine neue Version von AudioFlow Matrix ist verfügbar: %s (du hast %s).",
      "È disponibile una nuova versione di AudioFlow Matrix: %s (hai la %s).",
      "Доступна новая версия AudioFlow Matrix: %s (у вас %s)."}},
    {{"Download", "Descargar", "Baixar", "Télécharger", "Herunterladen", "Scarica", "Скачать"}},
    {{"Ignore this version", "Ignorar esta versión", "Ignorar esta versão", "Ignorer cette version",
      "Diese Version ignorieren", "Ignora questa versione", "Пропустить эту версию"}},
    {{"Later", "Más tarde", "Mais tarde", "Plus tard", "Später", "Più tardi", "Позже"}},

    // ---- Start-up errors ----
    {{"Select a master ASIO driver in the Devices tab.", "Elige un driver ASIO maestro en la pestaña Dispositivos.",
      "Escolha um driver ASIO mestre na aba Dispositivos.",
      "Choisissez un pilote ASIO maître dans l'onglet Périphériques.",
      "Wähle im Tab Geräte einen Master-ASIO-Treiber.", "Scegli un driver ASIO master nella scheda Dispositivi.",
      "Выберите главный ASIO-драйвер на вкладке «Устройства»."}},
    {{"The ASIO driver did not report its buffer size; using %u samples.",
      "El driver ASIO no informó de su tamaño de búfer; se usan %u muestras.",
      "O driver ASIO não informou o tamanho do buffer; usando %u amostras.",
      "Le pilote ASIO n'a pas indiqué sa taille de tampon ; %u échantillons utilisés.",
      "Der ASIO-Treiber hat keine Puffergröße gemeldet; es werden %u Samples verwendet.",
      "Il driver ASIO non ha indicato la dimensione del buffer; uso %u campioni.",
      "ASIO-драйвер не сообщил размер буфера; используется %u сэмплов."}},
    {{"Microphone: %s", "Micrófono: %s", "Microfone: %s", "Microphone : %s", "Mikrofon: %s", "Microfono: %s",
      "Микрофон: %s"}},
    {{"Loopback: %s", "Loopback: %s", "Loopback: %s", "Loopback : %s", "Loopback: %s", "Loopback: %s", "Loopback: %s"}},
    {{"Stream output: %s", "Salida Stream: %s", "Saída Stream: %s", "Sortie Stream : %s", "Stream-Ausgang: %s",
      "Uscita Stream: %s", "Выход Stream: %s"}},
    {{"Too many channels for the routing matrix (max %u).", "Demasiados canales para la matriz de ruteo (máx. %u).",
      "Canais demais para a matriz de roteamento (máx. %u).", "Trop de canaux pour la matrice de routage (max %u).",
      "Zu viele Kanäle für die Routing-Matrix (max. %u).", "Troppi canali per la matrice di routing (max %u).",
      "Слишком много каналов для матрицы маршрутизации (макс. %u)."}},
    {{"Driver registration failed.", "Falló el registro del driver.", "O registro do driver falhou.",
      "L'enregistrement du pilote a échoué.", "Treiberregistrierung fehlgeschlagen.",
      "Registrazione del driver non riuscita.", "Не удалось зарегистрировать драйвер."}},
};

Language g_language = Language::English;
// Translated text (with its ID suffix) per key as written in the code. GUI thread only;
// cleared when the language changes. Node-based, so returned pointers survive inserts.
std::unordered_map<std::string, std::string> g_cache;

const std::unordered_map<std::string_view, const Entry*>& index() {
    static const auto map = [] {
        std::unordered_map<std::string_view, const Entry*> m;
        for (const Entry& e : kEntries) m.emplace(e.text[0], &e);
        return m;
    }();
    return map;
}

} // namespace

std::span<const LanguageInfo> languages() { return kLanguages; }
std::span<const Entry> entries() { return kEntries; }

Language fromCode(std::string_view code, Language fallback) {
    for (const LanguageInfo& l : kLanguages) {
        if (code == l.code) return l.language;
    }
    return fallback;
}

const char* code(Language language) { return kLanguages[static_cast<int>(language)].code; }

Language systemLanguage() {
    switch (PRIMARYLANGID(GetUserDefaultUILanguage())) {
    case LANG_SPANISH: return Language::Spanish;
    case LANG_PORTUGUESE: return Language::Portuguese;
    case LANG_FRENCH: return Language::French;
    case LANG_GERMAN: return Language::German;
    case LANG_ITALIAN: return Language::Italian;
    case LANG_RUSSIAN: return Language::Russian;
    default: return Language::English;
    }
}

void setLanguage(Language language) {
    if (language == g_language) return;
    g_language = language;
    g_cache.clear();
}

Language language() { return g_language; }

const char* tr(const char* text) {
    if (g_language == Language::English) return text;
    if (const auto it = g_cache.find(text); it != g_cache.end()) return it->second.c_str();

    const std::string_view full(text);
    const std::size_t id = full.find("##");
    const std::string_view key = full.substr(0, id);
    std::string translated(key);
    if (const auto it = index().find(key); it != index().end()) {
        translated = it->second->text[static_cast<int>(g_language)];
    }
    if (id != std::string_view::npos) translated += full.substr(id);
    return g_cache.emplace(text, std::move(translated)).first->second.c_str();
}

} // namespace afm::app::i18n
