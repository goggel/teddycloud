# Plan 002: Vier Audioquellen mit eigenen Einstellungen und gespeicherten Feednamen

> **Ausführungsstand 2026-10-11:** Isoliert implementiert; alle automatisierten
> Prüfungen bestanden. Auf ausdrücklichen Benutzerwunsch in den lokalen Branch
> `feature/prepared-podcast-tonies` integriert. Browser- und physische
> Toniebox-Abnahme weiterhin offen. Beide Featurebranches wurden anschließend
> auf ausdrücklichen Benutzerwunsch zu ihren Origins gepusht.
> Der folgende Plan bleibt die verbindliche Spezifikation; Prüfnachweise stehen
> im [Ausführungsreview](002-execution-review.md).

## Status und Arbeitsverzeichnisse

- **Priorität:** P1
- **Aufwand:** L; grob 8–12 Entwicklungstage einschließlich dauerhafter Quellenprofile,
  Kompatibilität und automatisierter Prüfungen. Geräteabnahme kommt hinzu.
- **Risiko:** MED; besonders Quellenwechsel, parallele Podcast-Jobs und Metadatenpersistenz.
- **Kategorie:** direction / architecture / UX
- **Abhängigkeit:** Softwaregrundlage aus Plan 001 ist bereits implementiert.
  Dessen offene Geräteabnahme blockiert die Entwicklung nicht, bleibt aber offen.
- **Geplant am:** 2026-10-09
- **Workspace:** `/Users/benjaminkoster/git-privat/teddycloud`, HEAD `78c3b38`.
- **Backend:** Unterverzeichnis `teddycloud/`, HEAD
  `6fd7f9dbb4c4fca54bfdbb71a35ab4856352163f`, Branch `feature/prepared-podcast-tonies`.
- **Frontend:** Backend-Submodul `teddycloud/teddycloud_web/`, HEAD
  `8a30a31e43d1d71a4041c4c4cada676fc58fb918`, gleicher Branchname.
- Alle Anwendungspfade unten sind relativ zum **Backend**, sofern nicht anders benannt.
  Diese Plandatei liegt im äußeren Workspace. Befehle explizit im richtigen Repo ausführen.
- Der Backend-Status `M teddycloud_web` ist die erwartete lokale Integration.
  Nicht zurücksetzen, stagen oder als Featureänderung mitcommitten.
- Während der Planung kamen unabhängig davon uncommittete Änderungen in
  `src/podcast_feed.c`, `tests/test_podcast.c` und `docs/podcasts.md` hinzu:
  präzisere RSS-/Video-Fehlermeldungen und passende Tests/Dokumentation. Diese
  Änderungen erhalten und beim Driftabgleich berücksichtigen; sie stammen
  nicht aus dieser Planung und sind keine Umsetzung von Plan 002.

### Wiederaufnahme mit fokussierter Persistenzkorrektur — 2026-10-10

**Historischer Wiederaufnahmepunkt:** Der Executor hatte
die Umsetzung isoliert bis Backend `bfeaf04c` und Frontend `d306b1fa` gesichert.
Arbeitsverzeichnisse und unabhängige Prüfergebnisse stehen im
[Ausführungsreview](002-execution-review.md). Die ursprünglichen Anwendungsbranches
wurden nicht verändert.

**Aktueller Abschlussstand 2026-10-11:** Backend `8339af0e`, Frontend `d306b1fa`,
beide auf `feature/plan-002-four-sources-resumed` unter
`execution-worktrees/plan-002/backend` bzw. dessen `teddycloud_web/`.
Die fokussierte Persistenzkorrektur ist reproduziert, umgesetzt und unabhängig
geprüft; Reviewrunde 1 ist erledigt. Frontend (169 Tests, Typen, Format,
Übersetzungen, Build), Backend in beiden Featuremodi und alle angegebenen
C-/Python-Prüfungen bestehen. Produktionsimage `04eb430da969` aus dem sauberen
Integrationscheckout `8eb39cfc` wurde unabhängig erneut gebaut; der vollständige
Container-Harness besteht einschließlich Root/PUID=1000 und Neustartpersistenz.

**Offene Abnahme:** Der tatsächliche Browseraufruf für
<http://127.0.0.1:33513/web/> lieferte `No browser is available`.
Der isolierte Preview wurde anschließend auf Benutzerwunsch gestoppt. Die
physische Toniebox-Abnahme bleibt offen. Keine weitere Produktfrage oder
Codekorrektur steht aus; der Plan ist bis zur erforderlichen Abnahme nicht DONE.

**Benutzerbeauftragte lokale Integration 2026-10-11:** Die geprüften Commits
wurden konfliktfrei in Backend und Frontend auf `feature/prepared-podcast-tonies`
übernommen. Aktuelle HEADs: Backend `d372dcd7`, Frontend `0d31514`.
Das Backend enthält einen separaten lokalen Integrationscommit mit aktualisiertem
Frontendpointer und neu gebautem Webbundle. Dies ist die ausdrücklich beauftragte
lokale Fork-Integration; für spätere Upstream-PRs bleibt die Trennung der
Sourceänderungen von Bundle-/Pointeränderungen erforderlich.
Sourcevergleich mit den geprüften Ständen, 338 gebaute Webdateien (außer
Versionsmetadaten) und `make test_git_version` sind erfolgreich; beide
Arbeitsverzeichnisse sauber. Previewcontainer `tc-plan002-browser` ist `exited`;
seine isolierten Volumes bleiben erhalten. Anschließend wurden auf ausdrücklichen
Benutzerwunsch beide Featurebranches erfolgreich zu ihren `goggel`-Origins
gepusht (Frontend zuerst, Backend danach); beide stimmen mit den Remotebranches überein.

Die bisherige Annahme einer gemeinsamen Sperre für alle Content-Mutationen ist
unvollständig: `src/handler.c:890` ruft `mutex_lock_id` auf, dessen Implementierung
in `src/mutex_manager.c:46` sofort zurückkehrt (Return in Zeile 49). `saveTonieInfo` (Schreibstelle
`src/handler.c:982`), Move-to-Library (`src/handler.c:1210`) und die
Modell-Neuzuordnung (`src/handler_api.c:4386`/`:4399`) können dadurch ein älteres
Content-JSON nach einem erfolgreichen Profil-CAS-Save schreiben und neuere
Profile einschließlich unbekannter Profilversionen überschreiben. Die Tests mit
dem normalen Content-Setter decken diese anderen Schreibpfade nicht ab.

**Beschlossene Planrevision nach Benutzerabstimmung:**

1. Die betroffenen Content-JSON-Schreibpfade und ihre Sperren/Lebenszeiten erfassen. Den
   Verlust einer neueren Profilrevision durch einen deterministisch veralteten
   Legacy-Snapshot als Regression reproduzieren; auch unbekannte Profilversionen
   einschließen, in beiden Podcast-Buildmodi.
2. Eine kleine gemeinsame Update-Hilfe verwenden: aktuelle Daten unter einer
   kurzen gemeinsamen Sperre neu lesen, ausschließlich beabsichtigte Feldänderungen
   anwenden und speichern. Unveränderte Felder, neue Profile und unbekannte
   Profilversionen erhalten. Bei einer beabsichtigten Quellenänderung eine
   zwischenzeitlich geänderte Zuordnung als Konflikt behandeln. Bestehende
   Podcast-Ownership-Prüfungen erhalten. `mutex_lock_id` nicht einfach aktivieren: die Funktion
   dokumentiert einen bestehenden TagIndex-Deadlock. Keine globale Content-Sperre
   über Netzwerkübertragung oder Encoding halten; Content → Service beibehalten.
   Eine ungeschützte Read/Merge/Write-Ergänzung in `save_content_json` reicht nicht.
3. Der Umfang ist jetzt gezielt für `saveTonieInfo`, Move-to-Library und
   Modell-Neuzuordnung erweitert; siehe Schritt 2a und die Dateigrenzen unten.
   Zuerst den tatsächlichen Verlust mit einer Regression nachweisen, anschließend
   die kleinste erforderliche Korrektur umsetzen. Keine allgemeine Multiuser-
   Architektur und kein Umbau des gesamten Mutex-Managers.
4. Erst nach bestandenem Lost-Update-Test die restlichen Namens-/Worker-/Recovery-
   Kriterien und alle unten aufgeführten finalen Build-/Containerprüfungen
   ausführen. Frühere Step-1–3-Images prüfen den finalen Backendstand nicht.

Diese Revision wurde am 2026-10-10 ausdrücklich zur Ausführung beauftragt.
Sie hebt nur den bisherigen Dateiumfang-Blocker für die beschriebenen Schreibpfade
auf. Weitere STOP-Bedingungen bleiben wirksam. Bestehende WIP-Commits erhalten;
nicht als vollständige Funktion mergen, veröffentlichen oder den Plan auf DONE setzen.

**Driftprüfung vor Beginn, vom äußeren Workspace:**

```sh
git -C teddycloud status --short
git -C teddycloud/teddycloud_web status --short
git -C teddycloud diff --stat 6fd7f9dbb4c4fca54bfdbb71a35ab4856352163f..HEAD -- src include tests docs DockerfilePodcastTests
git -C teddycloud/teddycloud_web diff --stat 8a30a31e43d1d71a4041c4c4cada676fc58fb918..HEAD -- src public/translations
```

Bei Drift die unten genannten Symbole mit dem aktuellen Code vergleichen.
Unabhängige Änderungen nicht überschreiben; bei einem widersprüchlichen Vertrag stoppen.

**Ausführungsabgleich 2026-10-09:** Beide HEADs entsprechen weiterhin den oben
genannten Baselines. Zusätzlich zu den ursprünglichen Hinweisen bestehen lokale
Feed-Inspection-Änderungen in `include/podcast_feed.h`, `src/podcast.c` und
`tests/test_podcast_api.py`. Die geprüften Content→Service-Locks, Overlay-Auflösung
und Podcast-Verträge stimmen weiterhin mit diesem Plan überein. Der Executor
übernimmt einen Snapshot sämtlicher vorhandener Backend-Änderungen in seinen
isolierten Worktree; diese Baseline zählt nicht zum Plan-002-Diff. Originalrepos
bleiben unangetastet. Die offene physische Geräteabnahme aus 001 bleibt offen.

## Ziel und verbindliche Produktentscheidungen

Im Tonie-Editor gibt es genau vier Quellen: **Audiodateien**, **Radio**,
**Podcast** und **ARD Audiothek / Sounds**. Jede Quelle hat ein eigenes Formular
und eigene Einstellungen. RSS-Podcast und ARD nutzen weiterhin denselben nativen
Download-/Trim-/TAF-Dienst. ARD meint Audio-Sendungen, keine Video-Mediathek.

| Quelle | Formular und Verhalten |
| --- | --- |
| Audiodateien | Bibliotheksauswahl, vorhandene Quellpfadbearbeitung, Modell-Audio-Zuordnung und passende Vorherige-/Nächste-Datei-Aktionen erhalten |
| Radio | Sendersuche, manuelle Stream-URL, ausgewählter Sendername; bestehendes Streamingverhalten und passende Live-/No-Cloud-Werte |
| Podcast | Apple-Verzeichnissuche oder RSS-URL, Feedname, Folgenanzahl, Reihenfolge, Intervall, Aktivierung, Standardschnitte und Folgen-Overrides |
| ARD Audiothek / Sounds | ARD-Suche oder Sendungslink, Sendungsname, dieselben Vorbereitungseinstellungen und source-gebundenen Schnitte |

1. Pro effektiver Content-Verzeichnis/rUID-Kombination bleibt **eine Quelle aktiv**
   und höchstens ein Podcast-Abonnement registriert. Vier gespeicherte Profile
   bedeuten nicht vier parallel laufende Quellen oder vier Abonnements.
2. Innerhalb des offenen Editors bleiben alle vier Entwürfe beim Umschalten erhalten.
   **Speichern** persistiert die Quellenprofile serverseitig und aktiviert die
   ausgewählte Quelle. Die zuletzt gespeicherten Einstellungen inaktiver Quellen
   stehen nach erneutem Öffnen, Browserwechsel und Serverneustart zur Verfügung.
   **Abbrechen** verwirft Änderungen; bloßes Umschalten sendet keine Mutation.
3. Nach Suchauswahl ist sofort der Podcast-/Sendungsname sichtbar. Er bleibt nach
   Speichern, Vorbereitung, erneutem Öffnen und Neustart erhalten. Die URL bleibt
   über ein beschriftetes Detailfeld sichtbar, kopierbar und bearbeitbar.
4. Podcast und ARD prüfen beim erkannten Aufstellen **immer** auf Änderungen,
   solange das Abonnement aktiviert ist. Der abschaltbare Schalter entfällt.
   Bestehende Abonnements mit `refreshOnPlacement=false` werden ebenfalls erfasst.
5. Die vorhandene 900-Sekunden-Drossel und Job-Deduplizierung bleiben erhalten.
   „Immer“ bezeichnet die verbindliche Aktivierung, nicht einen Download bei jedem
   wiederholten Aufstellen. Intervall `0` deaktiviert nur den periodischen Scheduler.
6. Aufstellen startet weiterhin eine Hintergrundprüfung; die Box kann zunächst
   vorhandene Audioinhalte spielen. Keine Zusage eines sofort aktualisierten
   Downloads oder neuer physischer Toniebox-Unterstützung.

## Ist-Zustand und belastbare Einstiegspunkte

- `teddycloud_web/src/components/tonies/toniecard/modals/EditTonieModal.tsx:276`:
  Quellenauswahl nur `audio` / `podcast`; im Audioformular stehen Dateiauswahl und
  `RadioStreamSearch` gemeinsam.

  ```tsx
  options={[
      { value: "audio", label: t("podcasts.normalAudio") },
      { value: "podcast", label: t("podcasts.title") },
  ]}
  ```

- `.../podcasts/PodcastSubscriptionEditor.tsx`: zweiter Provider-Select für
  `rss` / `ard_audiothek`; Suchauswahl setzt nur URL und teilweise Source-Key.
  Danach wird `setQuery("")` aufgerufen. Suchergebnisse haben bereits `title`
  und `author`; Preview hat bereits `title`.
- `.../podcasts/usePodcastSubscription.ts`: ein `draft`, Modus `audio|podcast`,
  Revision-Snapshot `editingBase`, Polling ohne Überschreiben des Entwurfs.
  Aktuell reicht ein vorhandenes Abonnement für `setMode("podcast")`.
  Das ist bei manuell pausierten, fremd zugeordneten Abonnements zu grob.
- `.../podcasts/podcastValidation.ts`: zentrale Defaults/Normalisierung;
  `refreshOnPlacement: false`; keine Anzeigenamen in `podcastConfig()`.
- `.../hooks/useTonieCardSaveFlow.ts`: Abonnementänderung vor direkter
  Quellzuordnung; `skipSourceSave` schützt verwaltete Quellen vor einem alten
  `selectedSource`. `selectedSource.startsWith("http")` beeinflusst Live-Werte.
- `teddycloud_web/src/types/podcastTypes.ts`: `PodcastSubscription.title`
  existiert, `PodcastConfig` hat noch keinen ausgewählten Anzeigenamen.
- `teddycloud_web/src/api/apis/TeddyCloudApi.ts:79`: handgeschriebene Podcast-Methoden;
  bestehendes Error-/Abort-/Overlay-Verhalten hier beibehalten.
- `src/podcast.c`: `normalize_config`, `request_impl`, `public_subscription`,
  `valid_registry`, `persist`, `podcast_placed`, `podcast_source_changed`.
  Speichern übernimmt eine feste Feldliste, in der Namen fehlen. `title` wird
  bei erfolgreicher Veröffentlichung, unverändertem Refresh und Recovery gesetzt.

  ```c
  /* request_impl: übernommene Konfigurationsfelder, Auszug */
  const char *keys[] = {"feedUrl", "sourceType", "sourceKey", "ardShowUrl", ...};
  /* podcast_placed: heutige abschaltbare Bedingung */
  if (yes(s, "enabled") && yes(s, "refreshOnPlacement") && ... &&
      podcast_now() - podcast_number(s, "lastPlacement") >= 900)
  ```

- `src/podcast_ard.c`: `podcast_ard_resolve()` liefert bereits `title` und
  kanonische Identität. RSS-/ARD-Preview liefern ebenfalls einen Titel.
- `src/contentJson.c`, `include/contentJson.h`: Content-Metadaten werden in
  `contentJson_t` eingelesen und explizit wieder serialisiert. Unbekannte Felder
  würden beim nächsten Speichern verloren gehen. `CONTENT_JSON_VERSION` ist 5.
- `podcast_content_lock/unlock` sind bei `PODCAST_SUPPORT=0` derzeit No-ops.
  `mutex_manager` bietet portable feste Mutex-IDs; seine dynamischen
  `mutex_lock_id/unlock_id` sind dagegen im Ist-Code deaktiviert und ungeeignet.
- `src/handler_api.c:4785`, `handleApiContentJsonSet`: Form-POST mit kleinem
  `item_data[256]`, Content-Lock, danach `podcast_source_changed`.
  Große Profilobjekte hier nicht hineinpressen.
- `src/server.c:141`: `request_paths[]` registriert Podcast-Endpunkte als
  `SERTY_WEB`. Neue Profil-Endpunkte müssen denselben Zugriffspfad nutzen.

Konventionen: React/TypeScript, Ant Design 6, i18next, vier Leerzeichen,
Vitest/jsdom mit echten Controls; C/cJSON und existierende Content-/Service-Locks.
`docs/architecture.md`, `docs/podcasts.md`, `docs/api/podcasts.md` und
`docs/webradio-streaming.md` beschreiben einen Server/Container, bestehende
Volumes, vorbereitete Kapitel und bisherige Stream-Klassifizierung. Diese
Verträge erhalten; keine Sidecars, Cronjobs oder neuen Laufzeitdienste einführen.

## Daten- und API-Vertrag

### Vier Quellenprofile

Frontend-Typ `AudioSourceKind = "audio" | "radio" | "podcast" | "ard"` ergänzen.
`podcast` bildet auf Backend-Provider `rss`, `ard` auf `ard_audiothek` ab.
Der UI-Typ ersetzt weder `ct_source_t` noch das bestehende Provider-Enum.

Persistenz pro Content-JSON, additiv und ohne Erhöhung von `_version`:

```json
{
  "sourceProfilesVersion": 1,
  "sourceProfilesRevision": 1,
  "sourceProfiles": {
    "audio": { "source": "lib://example.taf" },
    "radio": { "source": "https://example.org/live", "displayName": "Beispielradio" },
    "podcast": { "sourceType": "rss", "feedUrl": "https://example.org/feed", "displayName": "Beispielpodcast" },
    "ard": { "sourceType": "ard_audiothek", "ardShowUrl": "https://www.ardsounds.de/sendung/example/123/", "displayName": "Beispielsendung" }
  }
}
```

Podcast-/ARD-Profile enthalten zusätzlich die vollständige bestehende
Konfiguration einschließlich Schnitten, Overrides und Namensbindung. Obiges
JSON ist ein Strukturbeispiel, kein vollständiger Aktivierungsrequest.
Profile enthalten **keine** Subscription-ID, Job-ID, Revision des Abonnements,
Pins, prepared episodes oder internen Dateisystempfade.

- `sourceProfiles` in `contentJson_t` besitzen/kopieren/freigeben wie andere
  dynamische Felder; Load/Save-Roundtrip bei jeder normalen Content-Mutation,
  Podcast-Veröffentlichung und Recovery muss sie erhalten.
- Ein optionales Objekt mit fehlenden Feldern bedeutet ältere Daten: keine
  Content-Datei zurücksetzen, Profile zunächst aus aktiver Zuordnung/Subscription
  ableiten und erst bei explizitem Speichern materialisieren.
- Ein unbekanntes `sourceProfilesVersion` unverändert erhalten, Profilmutation
  verweigern und bestehende Wiedergabe weiter erlauben. Kein Herunterkonvertieren.
- Profile sind inaktive Einstellungen und lösen weder Netzwerkzugriff noch Jobs
  aus. Unvollständige inaktive Entwürfe dürfen strukturell begrenzt gespeichert
  werden; Aktivierung verlangt die vollständige vorhandene Quellenvalidierung.
- Aktive Quelle aus tatsächlicher Content-Zuordnung und Ownership ableiten;
  Profile sind keine zweite Wahrheit über Wiedergabe. Legacy-HTTP-Dateien und
  TAPs nicht pauschal in Radios umdeuten. Unklare Altquellen im bisherigen
  Audiodatei-/Pfadeditor verlustfrei editierbar lassen.

Neue Web-Endpunkte in eigenem Handler `handler_source_profiles.c/.h`:

| Methode/Pfad | Vertrag |
| --- | --- |
| GET `/api/source-profiles/capabilities` | `{supported:true,version:1,maxRequestBytes:65536}`; unabhängig von `PODCAST_SUPPORT` |
| GET `/api/source-profiles?ruid=...&overlay=...` | `{revision,profiles}`; aktuelles Content-JSON ist Persistenzquelle |
| POST `/api/source-profiles/save?overlay=...` | `{ruid,kind,expectedRevision,profile}` → `{revision,profiles}`; genau ein Profil pro Request, gemeinsamer Revision-Counter |

Nur GET/POST registrieren, spezifische Capability-Route vor dem Basisprefix.
Validierung für 16-stellige Hex-rUID, vorhandenen
Overlay und effektiven Content-Scope; dieselbe Content-Lock-Reihenfolge wie
existierende Content-Mutationen. Den Content-Lock für beide Buildmodi wirksam
machen: feste `MUTEX_CONTENT_JSON`-ID in `include/mutex_manager.h` ergänzen und
die vorhandenen `podcast_content_lock/unlock`-Wrapper auf diesen gemeinsamen
portablen Mutex umstellen. Reihenfolge Content → Service erhalten; nicht einen
zusätzlichen unabhängigen Lock nur für Profile einführen. Content-Auflösung aus `handleApiContentJsonBase`
gezielt wiederverwenden/extrahieren, ohne die Legacy-Endpunkte umzubauen.
JSON-Grenze 65.536 Bytes, NUL-/Chunked-Rejection und Fehlerform nach
`handler_podcast.c`. Keine stille Abschneidung langer URLs oder Namen.
Pro-Profil-Requests verhindern, dass zwei maximal große Trim-Konfigurationen
zusammen die bestehende Bodygrenze überschreiten. Konflikte 409, ungültige
Eingaben 400, Oversize 413, unbekannter Scope 404, Schreibfehler 503.

### Feedname direkt speichern und an die Quelle binden

`displayName?: string` und `displayNameSourceKey?: string` additiv in
`PodcastConfig`, Draft, Subscription und Quellenprofil aufnehmen.
Maximal 512 UTF-8-Bytes, kein NUL und keine Steuerzeichen. Nur als Text rendern.
Namen beeinflussen niemals URL-Validierung, Ownership oder TAF-Fingerprint.

- Suchauswahl setzt URL **und** Ergebnisname gemeinsam. RSS-Key aus normalisierter
  Feed-URL ableiten; ARD-Key aus Suchergebnis/Preview/Resolve verwenden.
  Fehlt ein ARD-Key beim Suchtreffer, den Titel vorläufig an den exakten
  Quelltoken binden und beim erfolgreichen Resolve/Preview kanonisch bestätigen.
- Backend übernimmt den gebundenen Anzeigenamen schon beim Save in die Registry
  und liefert ihn sofort in der Response, auch bevor die Vorbereitung fertig ist.
  ARD verwendet bevorzugt den bereits vom eigenen Resolve gelieferten Namen.
- Beim erfolgreichen Preview/Refresh wird ein nichtleerer Quelltitel für genau
  die noch aktuelle Revision/Identität übernommen. Ein fehlender Titel oder
  Netzwerkfehler darf einen vorhandenen korrekten Namen nicht löschen.
- Beim manuellen URL-Wechsel den bisherigen Namen/Key sofort invalidieren;
  erst Preview/Resolve oder neue Suche bindet einen Namen an die neue Quelle.
  Kein Titel von Quelle A darf neben URL B erscheinen.
- Der vorhandene `title` bleibt die Bezeichnung der zuletzt erfolgreich
  vorbereiteten Auswahl. Ein neu gewünschter Feed B kann `displayName=B` haben,
  während die bisher spielbare Vorbereitung A noch `title=A` hat.
- Fehlende Namensfelder bei alten Clients erhalten den Namen nur bei gleicher
  kanonischer Quelle; bei Quellenwechsel löschen. Optionalität darf alte
  RSS-/ARD-Clients nicht brechen. Registry v2 bleibt lesbar; neue Felder additiv.
- Bestandsdaten: aus dem aktuellen Prepared-Manifest nur dann übernehmen, wenn
  dessen `sourceKey` zur aktuellen Konfiguration passt. Sonst erst bei nächstem
  Preview/Refresh auflösen. Kein massenhaftes Netzwerk beim Serverstart.
- Anzeige: gewählter Name als primäre Auswahl, URL als sekundäres Detail;
  ohne Namen ehrlicher URL-Fallback. Prepared-Titel im Statusbereich separat
  anzeigen, falls die vorbereitete Quelle noch von der gewünschten abweicht.

### Aktualisieren beim Aufstellen immer aktiv

- `refreshOnPlacement` aus Kompatibilitätsgründen im API-/Registry-Schema behalten,
  aber auf `true` normalisieren. Fehlend oder explizit `false` alter Clients wird
  zu `true`; falsche Typen bleiben 400. Bei Updates nicht vom alten `false` erben.
- Bestehende gültige Registry-Einträge beim Start nach Recovery/Migration auf
  `true` setzen und mit bestehendem Backup-/Persistenzverfahren sichern.
  Keine neue Registry-Version nur für diesen additiven Policywechsel.
  Bei Speicherversagen die bestehende Fehlerbehandlung verwenden und keine
  erfolgreiche Migration vortäuschen. Unbekannte Versionen unangetastet lassen.
- In `podcast_placed` die optional abschaltbare Bedingung entfernen. Weiterhin
  `enabled`, Content-Scope, rUID, Queuegrenze, Deduplizierung, Abbruch und
  900-Sekunden-Grenze prüfen. Bereits pausierte/manuell ersetzte Quellen
  dürfen nicht durch Aufstellen wieder aktiviert werden.
- Frontend-Default und Save-Payload immer `true`; Schalter ersetzen durch den
  Hinweis „Beim Aufstellen automatisch prüfen; höchstens alle 15 Minuten“.
  Grenzen und bestehende Synchronisationshinweise erklären.

## Umfang und Grenzen

**Erlaubte Backend-Dateien:** `src/podcast.c`, `src/podcast_feed.c` nur für
Konfigurationsvalidierung, `src/contentJson.c`, `include/contentJson.h`,
`include/mutex_manager.h` nur für eine feste gemeinsame Content-Mutex-ID,
`src/handler_api.c`/`include/handler_api.h` für Content-Scope-Wiederverwendung und
die gezielte sichere Modell-Neuzuordnung aus Schritt 2a; `src/handler.c` und
bei Bedarf `include/handler.h` ausschließlich für die drei dort beschriebenen
Legacy-Persistenzpfade und deren Testintegration,
`src/server.c`, neue `src/handler_source_profiles.c`/`include/handler_source_profiles.h`,
neue Profil-Validierungshelfer `src/source_profiles.c`/`include/source_profiles.h`,
`tests/test_podcast_api.py`, `tests/test_podcast.c` nur für Konfigurations-/Namensvalidierung,
`tests/test_podcast_disabled.py`,
`tests/test_source_profiles.py` (neu), `tests/run_podcast_tests.sh` nur für die
neuen Serverprüfungen, `tests/run_podcast_container_tests.py`,
`DockerfilePodcastTests` nur für Testintegration, `docs/podcasts.md`,
`docs/api/podcasts.md`, `docs/api/source-profiles.md` (neu).
Makefile nur anpassen, falls neue C-Dateien nicht bereits automatisch erfasst werden.
Zusätzlich für Schritt 2a erlaubt: `tests/test_content_json_updates.c` (neu) und
ein dediziertes Makefile-Testtarget, falls der Nachweis nicht sinnvoll in den
vorhandenen API-Tests möglich ist. Keine Änderungen an `src/mutex_manager.c`.

**Erlaubte Frontend-Dateien:** `src/types/podcastTypes.ts`, neue
`src/types/audioSourceTypes.ts`, `src/api/apis/TeddyCloudApi.ts` und dessen Tests,
`src/components/tonies/toniecard/TonieCard.tsx`, `TonieCardTypes.ts`,
`modals/EditTonieModal.tsx`, `hooks/useTonieCardSaveFlow.ts`, vorhandene
`podcasts/*`-Dateien/Tests, neue `sources/*`-Editoren/State/Tests,
`public/translations/{de,en,es,fr,it,pl}.json`.
Radio-Suchkomponente/-Hook nur für kontrollierte Namens-/Auswahlübergabe erweitern.

**Nicht enthalten:** zentrale quellenunabhängige Bibliotheksverwaltung,
mehrere aktive Abonnements pro Tonie, ARD-Video, ARD-Kategoriebrowser,
Einzelfolgenimport, neue Audio-Schnittfunktionen, neue Radiosuche-Anbieter,
globale Encoder-Einstellungen pro Radio, neue Authentifizierung,
Änderungen an Cyclone/Opus/Ogg/Emscripten, Push/Deployment auf Haushaltsgeräte.

## Umsetzungsschritte

### 1. Quellenmodell und getrennte Entwürfe einführen

In `audioSourceTypes.ts` vier Typen mit jeweils passendem Profil definieren.
In `sources/useAudioSourceEditor.ts` Zustand für ausgewählte Art, vier Entwürfe,
Baseline und Profilrevision verwalten. Podcast-Abonnementrevision getrennt
halten. `usePodcastSubscription` darf Pollingstatus liefern, aber keine lokalen
Entwürfe oder die Auswahl nachträglich ersetzen. Provider immer aus der
ausgewählten Quellenart ableiten; kein zweiter Provider-Select im Zielmodell.
Bestehende Quellenerkennung in eine getestete Funktion extrahieren; vorhandene
Profile ergänzen fehlende Defaults ohne Quellpfade zu verändern.

Neue `sources/useAudioSourceEditor.test.tsx` und
`sources/sourceProfiles.test.ts` nach vorhandenen Hooktests anlegen. Prüfen:
vier voneinander unabhängige Entwürfe, Wechsel ohne Mutation, Cancel,
RSS-/ARD-Provider-Mapping, unklare Legacy-Pfade, Polling und Revisionskonflikte.

**Prüfung, im Frontend:**
`npm test -- src/components/tonies/toniecard/sources` und `npx tsc --noEmit`
→ neue Modelltests grün, keine Typfehler; alte Oberfläche darf bis Schritt 4 bleiben.

### 2. Serverseitige Profile und gebundene Namen ergänzen

Content-JSON um additive Profilfelder erweitern, einschließlich vollständigem
Load/Save/Free-Roundtrip und Roundtrip unbekannter Profilversionen. Die drei
Profil-Endpunkte registrieren und nach obigem Vertrag implementieren. Keine
Profilmutation über das alte Form-POST. API-Client und Metadatennormalisierung
ergänzen; Profile dürfen nur bekannte konfigurative Felder enthalten.

`normalize_config`, Save-Feldliste, Public-Response und alle drei Stellen für
Titelübernahme im Podcast-Service (Publikation, unveränderter Refresh, Recovery)
um die beschriebenen Namensregeln erweitern. Namen nach erfolgreicher
Persistenz zurückliefern; vor API-Success weder Revision noch Name vortäuschen.
`displayName`/Key in `podcastConfig` und Capability-Behandlung erhalten.
Neue optionale Capability `displayNameSupported` für alte Backendversionen;
alte Server bekommen keine neuen Namensfelder und behalten URL-Fallback.

`tests/test_source_profiles.py` mit eigener isolierter Serverumgebung nach
`tests/test_podcast_api.py`/`tests/py/with_server.sh` erstellen. Den neuen Test
in den dedizierten Container-Testablauf einbinden. Er muss auch mit
`PODCAST_SUPPORT=0` für Audio-/Radioprofile laufen; inaktive Podcast-Profile
dürfen speicherbar sein, aktivieren aber keine deaktivierte Funktion.

**Prüfung, im Backend nach Imagebau mit dem unten genannten Befehl:**
`podman run --rm localhost/teddycloud:four-sources-tests make test_podcast`
→ bestehende und neue Podcast-/Namens-/Profiltests grün.
`podman run --rm localhost/teddycloud:four-sources-tests bash tests/py/with_server.sh 0 0 0 180 -- python3 tests/test_source_profiles.py`
→ alle Profile-/CAS-/Roundtrip-/Overlaytests grün. Der Imagebau muss auch den
feature-disabled Testweg enthalten und erfolgreich abschließen.

### 2a. Veraltete Legacy-Snapshots gezielt absichern

**Ausgangsbasis:** isolierter Backend-Checkpoint `bfeaf04c`, Frontend `d306b1fa`;
die bisherigen Implementierungen und unabhängigen Frontendprüfungen erhalten.
Dieser Schritt geht allen weiteren finalen Backend-/Containerprüfungen voraus.

**Regression zuerst:** In `tests/test_source_profiles.py` einen tatsächlichen
Legacy-Schreibpfad zwischen Lesen und Schreiben anhalten, parallel per Profil-API
speichern, dann den alten Schreibpfad fortsetzen. Geeignet ist `saveTonieInfo`
beim automatischen Aktualisieren von Content-Metadaten. Eine ausschließlich bei
`PODCAST_TESTING=1` einkompilierte Synchronisationshilfe darf den gezielt benannten
Test-Tonie vor dem Speichern anhalten. Keine Produktionstest-Route, keine
Timing-/Sleep-only-Behauptung. Alternativ ist ein C-Test erlaubt, der den echten
Legacy-Persistenzcode mit einem deterministisch veralteten Snapshot ausführt.
Der Test muss vor der Korrektur die verlorene Profilrevision nachweisen und
danach unveränderte neue Profile plus erfolgreiche beabsichtigte Metadatenänderung
prüfen. Auch einen vorher gelesenen Snapshot ohne Profilfelder sowie aktuelle
opaque Daten mit unbekannter `sourceProfilesVersion` prüfen.

**Kleinste Korrektur:** Eine gemeinsame Update-Hilfe in `contentJson.c/.h`
für die bisher ungeschützten Legacy-Schreibstellen ergänzen. Sie verwendet
denselben `MUTEX_CONTENT_JSON` wie Profil-CAS und Podcastpublikation. Sie liest
die aktuellen Content-Daten unter der Sperre, wendet ausschließlich die
beabsichtigten Felder an und schreibt. Eine Sperre nur um den alten Snapshot-
Write behebt den Fehler nicht. Die Änderungsliste muss explizit oder aus einer
besitzenden Kopie des ursprünglichen persistierten Zustands ableitbar sein;
abgeleitete Laufzeitfelder niemals versehentlich als komplettes Snapshot-Update
behandeln. Vorhandene frisch gelesene Content→Service-Transaktionen dürfen ihre
bewährte Sperrreihenfolge behalten; ein bereits gesperrter interner Helfer muss
klar von einem selbst sperrenden Einstieg getrennt sein, falls notwendig.

`saveTonieInfo`/`freeTonieInfo` dürfen keinen vor Podcast- oder Profiländerungen
gelesenen Komplettzustand zurückschreiben. Move-to-Library darf nur seine
beabsichtigte Quellenänderung anwenden. Modell-Neuzuordnung darf nur das noch
passende Modell umbenennen. Andere aktuelle Felder bleiben erhalten. Wurde die
zu ändernde Quellenzuordnung inzwischen ersetzt, diese erhalten und mit dem
vorhandenen Fehler-/Logging-Muster einen Konflikt melden. Keine blinde Wiederholung.
Keine neue UI oder allgemeines Konfliktprotokoll für diesen internen Fix.

**Grenzen:** ID-Locks bleiben unverändert deaktiviert. Keine globale Sperre über
die Lebenszeit eines `tonie_info_t`, Netzwerkzugriffe, Streaming oder Encoding.
Keine neue Registry, Datenbank, Multiuser-Sitzung oder externe Abhängigkeit.
Keine zweite unabhängige Profil-Sperre. Beschädigte/neuer unbekannter Content
darf nicht durch einen Merge stillschweigend auf Defaults zurückgesetzt werden.

**Nachweise:** Legacy-Metadatenupdate und Profil-Save erhalten beide Ergebnisse;
Legacy-Metadatenupdate erhält eine zwischenzeitlich ersetzte Quelle; veraltete
beabsichtigte Quellenänderung überschreibt sie nicht; Modell-Neuzuordnung erhält
Profile und andere Felder. Vergleichbare Source-Ownership-Fälle mit vorhandenen
Podcast-Tests abdecken. PODCAST_SUPPORT=1 und =0 prüfen. Testpause muss bei
fehlender Freigabe begrenzt abbrechen und vollständig aus Produktionsbinary und
Runtime-Konfiguration verschwinden.

**Prüfung:** Der erweiterte API-Test ist in `make test_podcast` und dem bereits
vorhandenen Disabled-Profiltest enthalten; beim Disabled-Test darf hierfür
`PODCAST_TESTING=1` separat gebaut werden. Der bisherige echte Produktions-
Disabled-Build mit `PODCAST_TESTING=0` und ohne curl/XML-Linkage bleibt geprüft.
Nach dem finalen Imagebau beide Fixture-Builds unabhängig ausführen:

```sh
podman run --rm localhost/teddycloud:four-sources-tests env TC_SERVER_BIN=/buildenv/bin/podcast-test/teddycloud TC_LEGACY_RACE_TESTING=1 bash tests/py/with_server.sh 0 0 0 180 -- python3 tests/test_source_profiles.py
podman run --rm localhost/teddycloud:four-sources-tests env TC_SERVER_BIN=/buildenv/bin/podcast-disabled-test/teddycloud TC_LEGACY_RACE_TESTING=1 bash tests/py/with_server.sh 0 0 0 180 -- python3 tests/test_source_profiles.py
```

Beide müssen ohne übersprungene Legacy-Regression grün sein. Der Imagebau
führt zusätzlich die normalen Disabled-API-/Profiltests mit T0 aus.
Falls ein dedizierter C-Test nötig wird, exakten Befehl in diesem Abschnitt
ergänzen und in `DockerfilePodcastTests` ausführen. Anschließend die bestehenden
Schritt-2-/Schritt-5-/Schritt-6-Gates ausführen. Nicht die gesamte ursprüngliche
Implementierung neu schreiben. STOP nur, wenn der fokussierte Fix tatsächlich
weitere nicht freigegebene Bereiche benötigt; den vorgeschlagenen kleinsten
Zusatzumfang konkret berichten.

### 3. Aufstellprüfung verbindlich aktivieren

Backend-Normalisierung und Bestandsübernahme nach obiger Policy umsetzen;
`podcast_placed` nicht mehr vom gespeicherten Opt-out abhängig machen.
`podcast_feed.c`-Validierung erlaubt fehlendes Legacy-Feld vor Normalisierung,
weist aber Strings/Zahlen zurück. Frontend-Defaults ändern, Schalter entfernen,
Hinweis und Capability `placementRefreshAlways` ergänzen. Bei alten Backends
Save trotzdem mit `true` senden; keine universelle Always-on-Garantie für
unveränderte Altserver anzeigen.

Service-Tests mit Fixture-Uhr und vorhandenen RTNL-Frames erweitern:
neues RSS, neues ARD, alter Eintrag `false`, Altclient sendet `false`, fehlendes
Feld, Intervall 0, disabled/paused, wiederholtes Aufstellen, Queue-Deduplizierung,
900-Sekunden-Grenze und Neustart. Keine echten Wartezeiten von 15 Minuten.

**Prüfung:** Testimage neu bauen; `podman run --rm localhost/teddycloud:four-sources-tests make test_podcast`
→ alle Szenarien grün, einschließlich Bestandsdatenmigration und bisherigen
Pins-/Crash-/Placementtests. Im Frontend `npm test` → keine alten Erwartungen
an den abschaltbaren Schalter mehr.

### 4. Vier getrennte Formulare und Namensanzeige integrieren

In `EditTonieModal` eine zugängliche Ant-Design-Auswahl mit vier Optionen
verwenden. `sources/AudioFileSourceEditor.tsx` und `RadioSourceEditor.tsx`
aus den vorhandenen Formularteilen extrahieren; kein Neuaufbau der Dateisuche.
`PodcastSubscriptionEditor` für RSS oder ARD parametrieren und den inneren
Provider-Select entfernen. Gemeinsame Podcast-Einstellungsfelder wiederverwenden.
Bei fehlender Podcast-Capability bleiben Audio/Radio nutzbar; ARD nur bei
passender `sourceTypes`-Capability auswählbar.

Suchauswahl als strukturiertes Ergebnis übernehmen, nicht nur als URL.
Kontrollierte Namensanzeige bleibt nach Aufklappen/Zuklappen und Suche nach
einem anderen Begriff sichtbar. Die bestehende Auswahl erst nach wirklicher
Neuauswahl ändern; abgebrochene Suche verändert weder Name noch URL.
Manuelle URL-Änderung invalidiert den alten Namen sofort. Späte Such-/Preview-
Responses für frühere Quelle oder geschlossenen Dialog ignorieren.

**Prüfung, im Frontend:**
`npm test -- src/components/tonies/toniecard` und `npm run check:translations`
→ neue gerenderte Tests für vier Formulare, Namen und immer aktive Prüfung
grün; Schlüssel in allen sechs vorhandenen Sprachen vollständig.

### 5. Speichern, Ownership und Quellenwechsel absichern

Save-Flow explizit nach Zielquellenart statt ausschließlich nach URL-Präfix
steuern. Geänderte Profile zunächst einzeln mit CAS speichern; jede bestätigte
Profilrevision übernehmen, kein blindes automatisches Wiederholen nach 409.
Danach die Zielquelle über bestehende Quellen-/Abonnementoperationen aktivieren:

| Wechsel | Aktivierung |
| --- | --- |
| Audio ↔ Radio | Direkte Quellzuordnung mit passenden, expliziten Live-/No-Cloud-Werten; vorhandenes Content-Streaming für Nicht-TAF-Dateien/TAPs erhalten |
| Audio/Radio → Podcast/ARD | Subscription Save mit Zielprofil; keine alte `selectedSource` danach schreiben |
| Podcast ↔ ARD | Bestehende Subscription-ID und `expectedRevision` zum Umkonfigurieren nutzen; Originalzuordnung und letzte vollständige Vorbereitung erhalten |
| Podcast/ARD → Audio/Radio | Vorhandenes revisioniertes Detach durchführen, danach neue direkte Quelle samt expliziten Flags schreiben; Podcast darf sie anschließend nicht zurücküberschreiben |

Ein Quellenwechsel allein darf keine fremden Trim-Overrides übertragen.
RSS und ARD behalten eigene Profile; der bestehende Source-Key-Konflikt bleibt
für einen Feedwechsel innerhalb desselben Profils wirksam. Neue Quelle erst
aktivieren, wenn ihr eigenes Profil valide ist.

Die vorhandenen APIs bilden **keine atomare Gesamttransaktion** aus Profilen,
Detach, Quelle und Modell. Das hier nicht stillschweigend versprechen:
Bei jedem Teilfehler Editor offen halten, bestätigte serverseitige Zustände neu
lesen und eindeutig anzeigen, welche Profile gespeichert wurden und welche
Quelle tatsächlich aktiv ist. Niemals Erfolg melden oder Folgeoperationen mit
veralteter Subscription-/Profilrevision senden. Falls Detach erfolgreich war
und die neue Quellzuordnung scheitert, ist die wiederhergestellte Originalquelle
der tatsächliche Zustand; gespeicherte Profile ermöglichen den erneuten Versuch.

Aktivquellen-Erkennung muss manuell ersetzte/pausierte Subscriptions erkennen.
`public_subscription` bei Bedarf um ein berechnetes `ownsAssignment` erweitern,
ohne private Pfade preiszugeben; erstmals wartende Jobs und deren Originalquelle
gesondert behandeln. Profile nie allein als Beweis für aktive Verwaltung nutzen.

**Prüfung:** `npm test -- src/components/tonies/toniecard` → alle 12 gerichteten
Wechsel zwischen verschiedenen Quellen sowie Same-source-Save getestet;
CAS-, Detach-, Quellen- und Modellspeicherfehler lassen den Editor offen.
Testimage neu bauen und `make test_podcast` darin ausführen → Worker kann nach
Detach/Quellenwechsel auch bei gleichzeitigem Encoding keine neue Fremdquelle publizieren.

### 6. Integration, Dokumentation und lokale Abnahme

Dokumentation um vier Formulare, persistente Profile, Namensbindung, Always-on-
Aufstellprüfung, Legacy-Fallback und Teilfehlerverhalten ergänzen. Keine Aussagen
über bereits erfolgte physische Geräteabnahme. Neue Handler sind Web-only und
laufen in demselben Container mit denselben Volumes.

Frontend separat prüfen und bündeln. Backend-/Frontend-PRs nach README getrennt
halten; generierte Webassets und Submodulpointer nur in isolierter lokaler
Integration bündeln. Keine Commits/Pushes oder Releases ohne entsprechenden
Arbeitsauftrag. Den aktuell laufenden `teddycloud-dev` nicht für Tests zurücksetzen.
Produktions-Smoke in zusätzlichem Container mit eigenen Volumes/Ports ausführen.

**Prüfung:** die vollständigen Befehle unten erfolgreich ausführen. Im Browser
alle vier Formulare auf Deutsch/Englisch ansehen, Podcast und ARD auswählen,
Name unmittelbar prüfen, speichern, Editor/Browser neu öffnen und Container
neu starten. Quellenprofile und Namen müssen wieder erscheinen; kein
Placement-Schalter. Ein Abbrechen darf keine Profildatei verändern.

## Befehle und erwartete Ergebnisse

Diese Befehle sind aus vorhandenen Scripts/Make-Targets abgeleitet. Die finale
Ausführung und unabhängige Wiederholung sind im Ausführungsreview dokumentiert;
alle unten genannten automatisierten Gates haben Exitcode 0.

**Frontend, im Verzeichnis `teddycloud/teddycloud_web`:**

```sh
npm ci
npm test
npx tsc --noEmit
npm run format:check
npm run check:translations
npm run build
```

Alle Exitcode 0, keine Typ-/Format-/Übersetzungsfehler. Neue Tests müssen zusätzlich
zum bisherigen Bestand laufen; keine alten Prüfungen entfernen, um grün zu werden.

**Backend, im Verzeichnis `teddycloud`:**

```sh
podman build -f DockerfilePodcastTests -t localhost/teddycloud:four-sources-tests .
podman run --rm localhost/teddycloud:four-sources-tests make test_podcast
podman run --rm localhost/teddycloud:four-sources-tests make test_c
podman run --rm localhost/teddycloud:four-sources-tests bash tests/py/with_server.sh 0 0 0 180 -- python3 tests/test_source_profiles.py
podman run --rm localhost/teddycloud:four-sources-tests make test_py TESTS=custom_json NO_SANITIZERS=1
podman run --rm localhost/teddycloud:four-sources-tests make test_py TESTS=box_sim NO_SANITIZERS=1
podman run --rm localhost/teddycloud:four-sources-tests make test_py TESTS=taf_length NO_SANITIZERS=1
```

Alle Exitcode 0. Der Imagebau führt bereits Frontendprüfung, Podcasttests und
Feature-disabled-Build/-API-Prüfung aus. Neue Profiltests explizit integrieren.
Bestehende Fixture-Schalter bleiben ausschließlich in der Testbinary verfügbar.

**Produktion, ausschließlich im sauberen Integrationscheckout mit neuem Bundle:**

```sh
make test_git_version
podman build --build-arg NO_SANITIZERS=1 -f DockerfileUbuntu -t localhost/teddycloud:four-sources .
```

Anschließend vom Backend aus:

```sh
python3 tests/run_podcast_container_tests.py --image localhost/teddycloud:four-sources --test-image localhost/teddycloud:four-sources-tests --engine podman
```

Exitcode 0; Harness um Namens-/Profilpersistenz und Always-on-Capability erweitern.
Keine Testvolumes des laufenden Dev-Containers verwenden. Optionalen manuellen
Toniebox-1-Test gesondert mit Modell/Firmware/Datum dokumentieren; simulierte
Placement-Frames ersetzen keine physische Prüfung.

## Testfälle und maschinenprüfbare Fertigkriterien

- [x] Alle obigen Prüfkommandos erfolgreich; neue Profiltests sind im Testimage enthalten.
- [x] Gerenderte Tests: vier Quellenauswahlen zeigen ausschließlich ihre eigenen Felder;
  Audio-/Radioauswahl funktioniert auch bei nicht verfügbarem Podcastdienst.
- [ ] Tests beweisen Wechsel ohne Netzwerkmutation, unabhängige RSS-/ARD-Schnitte,
  Cancel, reopen, Browser-/Serverneustart und Overlay-Isolation der Profile.
  Automatisierte Komponenten-/API-/Serverneustarttests bestanden; reale
  Browserabnahme einschließlich Browserneustart steht noch aus.
- [x] Alle 12 gerichteten Quellenwechsel plus unverändertes Speichern sind geprüft.
- [x] Ausgewählte RSS-/ARD-Namen sind sofort sichtbar und vor Encoding persistiert;
  Preview, fehlende Titel, fehlgeschlagene Vorbereitung, URL-Wechsel, späte Antworten,
  gleiche kanonische ARD-Identität, Unicode und 512-Byte-Grenzen sind geprüft.
- [x] Prepared-Titel A und gewünschter Name B werden beim Feedwechsel unterschieden;
  reine Namensänderung erzeugt keine neue TAF-Generation.
- [x] Profil-CAS verhindert verlorene Änderungen; 64-KiB-Grenzen, ungültige Typen,
  unbekannte Profilversionen und vollständige Content-JSON-Roundtrips sind geprüft.
  Zwei parallele Saves mit gleicher Revision ergeben einmal Erfolg und einmal
  409, auch bei `PODCAST_SUPPORT=0` und parallel zu einer Content-Mutation.
- [x] `refreshOnPlacement=false` aus Bestandsdaten oder Altclient wird zu `true`;
  fehlend wird akzeptiert, falscher Typ abgewiesen. Der Editor hat keinen Opt-out.
- [x] RSS und ARD prüfen beim Placement auch mit Intervall 0; 900-Sekunden-
  Drossel, Queue-Deduplizierung und Pausieren bleiben nachweislich wirksam.
- [x] Worker-Publikation, unveränderte Updates und Crash-Recovery erhalten Profile,
  Namensbindung, letzte vollständige Audiozuordnung und bestehende Downloadpins.
- [x] Neue Profil-Endpunkte bleiben bei `PODCAST_SUPPORT=0` für Audio/Radio nutzbar;
  Podcastaktivierung bleibt deaktiviert und die Binary ohne curl/XML-Linkage.
- [ ] Browserabnahme dokumentiert; physische Toniebox-Abnahme ausdrücklich als
  durchgeführt oder weiterhin offen gekennzeichnet.
  Browseraufruf blockiert (`No browser is available`); physische Abnahme offen.
- [x] Implementierungsdiffs im erlaubten Umfang; keine produktiven Daten/Certs/Volumes
  verändert. Bundle/Submodulpointer waren während des Source-Reviews auf den
  Integrationscheckout begrenzt und wurden anschließend auf ausdrücklichen
  Benutzerwunsch in einem separaten lokalen Integrationscommit übernommen.
- [x] Planstatus im äußeren `plans/README.md` aktualisiert.

## STOP-Bedingungen und Wartung

- Stoppen, wenn Ownership-/Lock-Reihenfolge oder effektiver Overlay-Scope nicht
  mit dem beschriebenen Stand übereinstimmen; keine zusätzliche globale
  Registry oder parallele Quellenverwaltung improvisieren.
- Stoppen, wenn unbekannte Profilversionen nur durch Löschen fremder Daten
  erhalten werden könnten oder Bestandseinträge unlesbar würden.
- Stoppen, wenn die neue Speicherung Änderungen außerhalb des erlaubten
  Umfangs erfordert oder eine Prüfung zweimal trotz gezielter Korrektur scheitert.
- Keine Atomizität über mehrere existierende APIs behaupten. Falls vollständiges
  Alles-oder-nichts-Speichern erforderlich wird, dafür einen separaten Plan mit
  dauerhafter Transaktionsabsicht/Recovery entwerfen.
- Künftige Podcastfelder müssen in Profiltyp, Normalisierung, API und Deep-Copy
  konsistent ergänzt werden. Namensmetadaten bleiben außerhalb des Fingerprints.
- Always-on-Placement ist eine serverseitige Produktregel. Nur den UI-Default
  zu ändern würde Altclient-/Bestandsdatenverhalten nicht erfüllen.
- Nicht auditiert: allgemeine Sicherheit/Performance des Gesamtprojekts,
  neue Radiostream-Semantik, ARD-Video und physische Toniebox-Wiedergabe.
