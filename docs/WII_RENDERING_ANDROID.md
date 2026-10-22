# Caricamento Wii e renderer Android

Analisi del 22 ottobre 2026 per il tester 0.0.4-a34.8. Le fonti sono codice
dei progetti che implementano queste funzioni e documentazione Vulkan.
Le osservazioni sul port sono verificabili nei file citati; non costituiscono
una cattura del comportamento del telefono.

## Cosa carica Wii

Occorre distinguere tre operazioni:

| Operazione | Wii | Questo port Android |
| --- | --- | --- |
| Lettura dei file | Richieste DVD con buffer, offset, priorità e callback; esiste anche un percorso sincrono che attende il completamento [1] | `dvd_read.cpp` usa una coda e un worker `pread`; il completamento ritorna attraverso l’interrupt DVD del port |
| Preparazione delle risorse | Il codice del gioco monta archivi e costruisce i resource holder; può attendere il lavoro asincrono | Lo stesso codice ricompilato usa i file convertiti durante l’estrazione |
| Configurazione grafica | Il gioco invia registri e comandi GX al TEV, che esegue le combinazioni previste dall’hardware [2] | Il recorder cattura i comandi e Vulkan li esegue con shader e pipeline del driver Android |

Nel codice locale, `ResourceHolderManager::createAndAddInner` e
`createAndAddInnerStationed` mostrano montaggio, preparazione asincrona e attesa.
`JKRDvdRipper` distingue inoltre lettura e decompressione. Non è corretto
attribuire ogni scatto alla lettura del disco: anche allocazioni, compilazione
di pipeline e sincronizzazione GPU possono fermare un fotogramma.

Wii non deve compilare un nuovo shader Android quando cambia configurazione
TEV. Il port traduce quel comportamento in un’altra API grafica. L’approccio
ibrido descritto da Dolphin usa shader generici mentre prepara quelli
specializzati [2]. Il port mantiene questa strategia, con un worker e un
avvio limitato; non prova a compilare l’intero gioco prima del titolo.
Questo riduce una classe di attese, ma non dimostra che un telefono possa
eseguire qualunque stato grafico senza costo aggiuntivo.

## Copie degli effetti: il problema trovato

`GX_CopyTex` copia una regione dell’EFB, il framebuffer interno, verso una
texture. Formato, intensità, dimezzamento e cancellazione sono parte del
comando [3]. Gli effetti possono poi campionare quella cattura. Non si tratta
di una texture HD da caricare dal disco. Dolphin gestisce questi passaggi
con conversioni specifiche per formato, alpha e profondità [4].

Il vecchio percorso Android eseguiva un blit RGBA per tutte le copie:
intensità, selezione dei canali e profondità venivano ignorate. Inoltre il
recorder scartava le copie dopo 8 frame e il renderer dopo 16. Decodificare
la RAM a quel punto non ricrea i pixel che erano rimasti solo sulla GPU.
Questi sono errori riproducibili nel codice; l’associazione a ciascun difetto
visibile nello screenshot resta da verificare con la scena originale.

Il componente nuovo comprende:

- `gx_efb_shader.cpp`: conversione compute con quantizzazione RGB8/RGBA6/RGB565,
  profondità a 24 bit, intensità e formati R4/R8/RA4/RA8/RGB565/RGB5A3/RGBA8,
  A8/G8/B8/RG8/GB8; il dimezzamento usa la media dei pixel sorgente.
- `vk_efb_copy_pipeline.cpp` e `vk_efb_copy_dispatch.cpp`: una pipeline compute
  preparata all’inizializzazione, viste colore/profondità e barriere esplicite.
  Le copie restano sulla GPU, senza readback sincrono per ciascun effetto.
- `gx_recorder_efb.cpp`: validità basata sulla memoria osservata e sulle
  referenze registrate, senza scadenza arbitraria in fotogrammi.
- `vk_copy_texture.cpp`: una nuova immagine sostituisce la precedente solo
  dopo che immagine, memoria e vista sono state create correttamente.

## Stabilità e illuminazione

I descriptor set Vulkan non possono essere liberati mentre comandi che li
usano sono ancora in esecuzione [5]. Liberare risorse registrate invalida
anche i command buffer che vi fanno riferimento [6]. Il port poteva
liberare set durante una copia, azzerare il pool a metà registrazione e
riusare descrittori collegati a viste distrutte.

`vk_image_sets.cpp` ora amplia i pool senza reset durante la registrazione.
La rimozione dalla cache è immediata; la liberazione effettiva attende il
completamento dei submit. Le viste eliminate invalidano anche i rispettivi
descrittori. Sono corretti difetti di gestione delle risorse; senza un trace
del dispositivo non è dimostrato che fossero l’unica causa del crash su
“Play This File”.

Nel vertex shader è corretto anche il caso di CLR1 presente senza CLR0:
il primo canale usa CLR1; il secondo richiede entrambi gli attributi, come nel
generatore di Dolphin [7]. Il test confronta sia il percorso specializzato
sia quello generico. Non sostituisce l’ispezione di tutti i materiali di Mario.

## Verifica e limiti

Il test EFB esegue il dispatch Vulkan di produzione su Mesa lavapipe e confronta
i byte con un calcolo indipendente: 4 formati sorgente × 2 dimensionamenti ×
13 formati destinazione = 104 casi. Altri test verificano saturazione dei pool,
ritiro differito, riuso di una vista, sostituzione fallita e persistenza delle
copie. Non sono misure di velocità su Mali.

Restano da implementare o verificare filtro verticale completo della copia,
gamma, stride/reinterpretazioni, coerenza TMEM/RAM e letture CPU dell’EFB.
L’invalidazione della copia osserva ancora i primi 64 byte della memoria:
una modifica fuori da quel campione può sfuggire. Il clear selettivo solo
RGB o solo alpha richiede ulteriore lavoro. Non si dichiara equivalenza
completa al chip Wii, né assenza di stutter o crash sulla ROM reale.

Il profilo continua a usare risorse e risoluzione native, velocità e camera
originali, joystick fisso, puntatore touch e quattro pulsanti camera.
Al riavvio dopo un segnale fatale viene offerto un log esportabile con
segnale, indirizzi e ultime righe del gioco. L’handler scrive in un file
privato già aperto, evitando logger bloccanti e `sync()` globale.

## Fonti primarie

1. [devkitPro/libogc: dvd.c](https://github.com/devkitPro/libogc/blob/master/libogc/dvd.c),
   `DVD_ReadAbsAsyncPrio`, `DVD_ReadPrio`; [API DVD](https://libogc.devkitpro.org/dvd_8h.html).
   libogc è un’implementazione homebrew, non il sorgente proprietario di Nintendo.
2. [Dolphin: Ubershaders](https://dolphin-emu.org/blog/2017/07/30/ubershaders/),
   spiegazione del TEV e del costo della traduzione in shader moderni.
3. [devkitPro/libogc: gx.c](https://github.com/devkitPro/libogc/blob/master/libogc/gx.c),
   `GX_SetTexCopyDst`, `GX_CopyTex`, `GX_SetCopyFilter`.
4. Dolphin: [TextureConverterShaderGen.cpp](https://github.com/dolphin-emu/dolphin/blob/master/Source/Core/VideoCommon/TextureConverterShaderGen.cpp),
   [TextureConversionShader.cpp](https://github.com/dolphin-emu/dolphin/blob/master/Source/Core/VideoCommon/TextureConversionShader.cpp),
   [TextureCacheBase.cpp](https://github.com/dolphin-emu/dolphin/blob/master/Source/Core/VideoCommon/TextureCacheBase.cpp).
5. [Khronos: vkFreeDescriptorSets](https://docs.vulkan.org/refpages/latest/refpages/source/vkFreeDescriptorSets.html),
   VUID 00309 e appartenenza del set al pool di origine.
6. [Khronos: Command Buffers](https://docs.vulkan.org/spec/latest/chapters/cmdbuffers.html),
   durata degli oggetti referenziati dai comandi.
7. Dolphin: [VertexShaderGen.cpp](https://github.com/dolphin-emu/dolphin/blob/master/Source/Core/VideoCommon/VertexShaderGen.cpp),
   selezione di `in_color_0`/`in_color_1`;
   [LightingShaderGen.cpp](https://github.com/dolphin-emu/dolphin/blob/master/Source/Core/VideoCommon/LightingShaderGen.cpp).
