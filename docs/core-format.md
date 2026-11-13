# Motor de respaldo de NinMagic Guardian (formato 1)

## Alcance

El mismo motor C se usa desde el NRO y el rescatador de Hekate. Solo accede a
archivos normales de la partición FAT de la microSD. No lee ni escribe NAND,
BOOT0/BOOT1, particiones eMMC/emuMMC, claves, firmware de Horizon ni partidas.
No es una imagen del sistema operativo instalado ni un restaurador de firmware.

Restaurar un respaldo anterior **no desinstala juegos añadidos después**:
`Nintendo`, la base de títulos de NAND y las particiones de juegos no se tocan.
Tampoco elimina archivos nuevos ajenos a la lista del respaldo. Esto significa
que un tema o módulo nuevo que impida arrancar no desaparece solo por restaurar
estos archivos. Identificar y aislar ese complemento requiere otra acción
explícita; el motor no adivina qué causó un cierre ni borra archivos sospechosos.

## API y adaptadores

`include/guardian_core.h` es el contrato C compartido. Las rutas son relativas a
la raíz SD, con `/` y sin `/` inicial. El adaptador debe rechazar enlaces,
reparse points y otras entradas especiales, incluida cualquier carpeta padre.
`rename` no debe sobrescribir un destino existente. `open('x')` es exclusivo,
nunca trunca. `file_sync` confirma contenido y `sync` confirma metadatos del
volumen. El rescatador y el NRO deben representar el mismo CID físico de 16 bytes
como 32 dígitos hexadecimales. Desconocer la identidad bloquea toda escritura.

- `gc_scan_source`: solo lee y estima archivos/bytes. No crea carpetas.
- `gc_inspect`: solo lee el manifiesto, y verifica los datos si se solicita.
  Permite CID vacío o distinto y devuelve la identidad del respaldo para que el
  rescatador resuelva la representación endian de **su mismo CID físico**.
- `gc_create_backup`: crea una copia nueva y sustituye la anterior después de
  verificarla por completo.
- `gc_recover_backup`: termina una sustitución interrumpida del respaldo. No
  restaura archivos del entorno de arranque.
- `gc_restore`: verifica todo y restaura; reanuda su propio diario si se cortó.

El motor comprueba la identidad de la tarjeta antes de restaurar o promover
copias. Modelo y versión de firmware se guardan como información; la UI debe
explicar sus diferencias y establecer las restricciones necesarias. Los metadatos
no demuestran compatibilidad de cualquier versión de Atmosphère con cualquier
firmware ni de cualquier paquete con todos los modelos.

Límites: 4096 archivos, ruta administrada de 240 caracteres ASCII, 128 MiB por
archivo, 8 GiB en total. Las rutas internas temporales pueden llegar a 511
caracteres. No se admiten segmentos `.`/`..`, vacíos, terminados en punto o
espacio, rutas absolutas, `\`, `:`, comodines o caracteres de control. Se rechazan
duplicados que solo difieren en mayúsculas, para evitar alias en FAT.

## Lista de archivos

Se incluyen automáticamente, si existen:

- `atmosphere/package3`, requerido y no vacío.
- `bootloader/hekate_ipl.ini`, requerido y no vacío.
- `bootloader/sys/**/*.bin`, `*.bso`, `*.pak`, `*.kipm`: Nyx y sus módulos y
  recursos, entre otros archivos de arranque con esas extensiones.
- `bootloader/update.bin`, `nyx.ini`, `patches.ini`, `ini/**/*.ini`.
- `atmosphere/config/**/*.ini`, `config_templates/**/*.ini`, `hosts/**/*.txt`.
- `atmosphere/reboot_payload.bin`, `fusee-secondary.bin`, `fusee-mtc.bin`.
- `exosphere.ini`, `emuMMC/emummc.ini` (solo la configuración, no la partición).
- Los nombres raíz conocidos `payload.bin`, `fusee.bin`, `hekate.bin`.

Se examinan los INI de Hekate. Las referencias HOS `fss0`, `pkg3`, `secmon`,
`warmboot`, `exofatal` y `kip1` deben apuntar a archivos existentes dentro de la
lista permitida; en caso contrario se explica la dependencia que falta y no se
declara completo el respaldo. Se admiten `atmosphere/kips/*.kip` y `*.kipm`, y
los comodines acotados `atmosphere/kips/*` o `atmosphere/kips/*.kip`.
`atmosphere/secmon.bin`, `warmboot.bin` y `fatal.bin` pueden incorporarse por
referencia explícita.

Las referencias a payloads conocidos de fusee/Hekate y al rescatador propio
`config/NinMagicGuardian/rescue/ninmagic-guardian-rescue.bin` se incluyen. La
referencia propia también exige y copia su `libsys_minerva.bso` exacto, para
conservar una pareja compatible de rescatador y módulo de memoria.
Otros payloads de herramientas o Linux/Android no obligan a copiar sus sistemas;
sus entradas INI se conservan. No se siguen rutas arbitrarias de esos sistemas.

Opciones explícitas:

- `GC_OPT_VISUALS`: imágenes BMP/JPG/PNG de `bootloader/res` y
  `bootloader/bootlogo.bmp`.
- `GC_OPT_PAYLOADS`: archivos BIN adicionales de `bootloader/payloads`, incluidos
  los que un INI referencie dentro de esa carpeta.

Se excluyen juegos, ports y datos de aplicaciones de `switch`, `Nintendo`,
Android/Linux, contenido eMMC/emuMMC y **todo** `atmosphere/contents` (incluidos
mods, cheats, temas y sysmodules). También se excluyen logs, informes de errores,
copias automáticas de claves y cualquier otra ruta no enumerada. Las carpetas
vacías no se guardan. La copia no promete reparar algo que quedó fuera.

## Estructura y manifiesto

```
config/NinMagicGuardian/backup/
  current/
    manifest.txt
    complete.sha256
    data/<ruta original relativa SD>
  staging/                 # únicamente durante una copia nueva
  previous/                # únicamente durante una sustitución
  promotion.pending        # intención durable de promover un SHA de manifiesto
```

Texto ASCII, LF obligatorio, decimal sin signo y hashes SHA-256 hexadecimales:

```
NMGUARDIAN1
identity=00112233445566778899aabbccddeeff
firmware=23.0.1
model=OLED
created=2026-11-13T12:00:00Z
options=0
count=2
bytes=12345678
F|12345600|<64 hex>|atmosphere/package3
F|78|<64 hex>|bootloader/hekate_ipl.ini
```

La lista está ordenada por ruta y no admite duplicados, rutas fuera de alcance,
totales incorrectos, líneas incompletas ni datos sobrantes. `complete.sha256`
contiene SHA-256 de los bytes exactos de `manifest.txt` y un LF (65 bytes).
Cada archivo se verifica individualmente. Estos hashes detectan corrupción;
**no son una firma de un editor** ni autentican archivos aportados por terceros.
Aunque se recalculen los hashes de un manifiesto manipulado, no se permite
restaurar fuera de la lista rígida del motor.

## Sustitución del respaldo

1. Verificar la copia actual y su CID, si existe.
2. Estimar espacio sin borrar la copia actual. Copiar a `staging` con creación
   exclusiva; confirmar, guardar índice y sello, verificar todos sus hashes.
3. Guardar `promotion.pending` mediante archivo temporal, sincronización y
   renombrado. El registro identifica el manifiesto nuevo exacto.
4. Renombrar `current` a `previous` y sincronizar; renombrar `staging` a `current`
   y sincronizar.
5. Verificar de nuevo `current`. Solo entonces retirar `previous` y la intención.

Un corte antes del paso 3 conserva seleccionada la copia actual. Un corte entre
renombrados se reanuda por el diario. Si la copia nueva se dañó y solo queda la
anterior verificada, se puede devolver esta a `current`; se retienen los restos
dañados hasta el siguiente respaldo explícito. Casos ambiguos o CID distinto
fallan conservando las copias. Durante el trabajo pueden coexistir dos copias;
al terminar queda una sola copia completa. No se promete recuperar una FAT o
tarjeta físicamente dañada mediante renombrados de archivos.

## Restauración interrumpible

`config/NinMagicGuardian/restore/pending.txt` guarda el hash del manifiesto
verificado. Los registros se publican con temporal + sincronización + rename;
una escritura corta no publica un diario parcial. Crear un respaldo nuevo se
bloquea mientras exista esa recuperación pendiente.

Para cada archivo se guarda un plan durable: ausencia original, o hash y tamaño
del archivo anterior. Se prepara una copia temporal del respaldo y se verifica.
Luego se mueve el destino anterior a `restore/old/<ruta>` y se confirma antes
de colocar el archivo recuperado. Se verifica el destino nuevo. Un corte se
reanuda consultando plan, copia temporal, archivo anterior y destino; no se pisa
un destino que haya cambiado por otra aplicación.

Los archivos anteriores se conservan como rollback hasta verificar **todos** los
destinos. Si algo falla durante ese proceso, puede haber una combinación parcial
de archivos y `recovery_required=1`: hay que volver a recuperar antes de iniciar
Atmosphère, no afirmar que la operación acabó. No es una transacción atómica de
todo el volumen. Al reintentar, los archivos ya correctos no se vuelven a mover.
Solo tras la verificación final se limpian los temporales y archivos anteriores.

No se retiran del sistema archivos ajenos al manifiesto, ni se modifica la base
de títulos. No debe restaurarse mientras otra aplicación actualice los mismos
archivos.

## Pruebas

`tests/run-core-tests.ps1` compila un adaptador host aislado y pruebas reales de
archivos en una carpeta temporal. Incluye vectores SHA-256 conocidos,
estimación sin escritura, exclusiones, CID desconocido/distinto, corrupción,
fallos de escritura, interrupciones de promoción/restauración y rechazo de un
manifiesto manipulado con su sello recalculado. El adaptador simula una entrada
especial para no requerir privilegio de enlaces simbólicos de Windows.

Estas pruebas no sustituyen pruebas físicas en V1/V2/Lite/OLED ni demuestran
durabilidad del hardware frente a desconexiones eléctricas reales.

## Inventario y aislamiento explícito de complementos de arranque

`guardian_addons.h` añade una acción separada de la restauración:

- `ga_capture` guarda **solo huellas y presencia**, tras un respaldo solicitado.
  No copia mods. El inventario tiene CID, SHA-256 del manifiesto actual y un
  sello de integridad propio. No se puede renovar mientras haya aislamiento
  activo. Un fallo de inventario no invalida por sí mismo los datos del respaldo;
  debe informarse por separado.
- `ga_inspect` compara el inventario con la SD sin escribir. Solo examina
  subcarpetas de 16 dígitos hexadecimales con `flags/boot2.flag`, y `romfs` del
  tema de HOME `0100000000001000`. Un marcador vacío es válido: la huella del
  módulo también incluye `exefs.nsp`, `rtld`, `main`, `main.npdm` y sus variantes
  bajo `exefs/`, con presencia/ausencia, tamaño y SHA-256. Por tanto detecta un
  ejecutable reemplazado aunque se conserve el marcador vacío.
- Los resultados significan **nuevo o cambiado**, no que se haya demostrado
  que ese complemento provoca un error. El inventario admite hasta 256 entradas;
  la revisión devuelve como máximo 32 candidatos. Superar límites bloquea una
  revisión parcial. Se leen como máximo 256 MiB por revisión y 128 MiB por
  archivo; el tema admite 4096 archivos y ocho niveles de carpetas.
- `ga_quarantine` exige que el usuario confirme una selección. Admite un
  subconjunto de una revisión, pero vuelve a obtenerla y comprueba cada ID,
  tipo, huella y hash del respaldo. No acepta candidatos inventados o caducados.
  Antes de mover cada entrada comprueba otra vez su huella.

El aislamiento mueve únicamente `flags/boot2.flag` del módulo o la carpeta
`romfs` del tema de HOME a
`config/NinMagicGuardian/quarantine/active/data/<ID>/...`. El ejecutable del
módulo sigue guardado en su lugar; desaparece su marcador de inicio. No se
mueven carpetas ROMFS de otros juegos, ni cheats, partidas o juegos. Se guarda un
plan completo y sellado mediante temporal + sincronización + rename **antes**
del primer movimiento. Un corte solo reanuda ese mismo plan confirmado, nunca
añade nuevas selecciones sin otra revisión.

`ga_restore_quarantine` reactiva las mismas entradas, comprobando de nuevo sus
huellas también cuando un paso ya se había completado. Las huellas del tema usan
rutas lógicas, de modo que cambiar de carpeta no las altera. En un módulo se usa
el marcador apartado y los ejecutables originales para verificar la huella.
Se bloquea si el archivo apartado cambió, si falta algo o si un nuevo archivo
ocupa el destino; no sobrescribe nada. Reactivar depende del CID y del plan
íntegro, **no requiere que el respaldo actual exista o tenga el mismo hash**.

Crear un respaldo nuevo se bloquea tanto en el motor como en la UI si existe un
plan activo, para conservar la referencia de comparación. El diario de aislamiento
se retira solo cuando todas las entradas han vuelto. Las carpetas vacías de
aislamiento pueden permanecer y no afectan al arranque. La integridad por SHA-256
no es una firma ni protege contra alguien que edite manualmente los registros
y recalcule sus hashes con acceso completo a la SD.

Las pruebas host incluyen subconjuntos válidos, selecciones inventadas/caducadas,
marcador vacío con ejecutable cambiado, CID distinto, plan interrumpido,
manipulación de archivos apartados, colisiones del destino, carpetas en lugar
del inventario y reactivación sin el sello del respaldo actual. También comprueban
que una ruta lógica larga del tema pueda moverse a la carpeta interna y volver.
