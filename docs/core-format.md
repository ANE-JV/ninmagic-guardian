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

Las referencias a payloads conocidos de fusee/Hekate se incluyen. La referencia
al rescatador propio
`config/NinMagicGuardian/rescue/ninmagic-guardian-rescue.bin` se valida junto con
su `libsys_minerva.bso` exacto, pero **ninguno de esos dos archivos se copia en
respaldos nuevos desde 0.1.1**. Su instalación y actualización son independientes
del respaldo del entorno. Así restaurar un respaldo anterior no reinstala un
rescatador antiguo que ya se corrigió.
Otros payloads de herramientas o Linux/Android no obligan a copiar sus sistemas;
sus entradas INI se conservan. No se siguen rutas arbitrarias de esos sistemas.

Opciones explícitas:

- `GC_OPT_VISUALS`: imágenes BMP/JPG/PNG de `bootloader/res` y
  `bootloader/bootlogo.bmp`.
- `GC_OPT_PAYLOADS`: archivos BIN adicionales de `bootloader/payloads`, incluidos
  los que un INI referencie dentro de esa carpeta.

Se excluye **toda la carpeta `switch`**, incluidos NRO de utilidades, juegos,
ports y datos de aplicaciones. También se excluyen `Nintendo`,
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

Compatibilidad con respaldos 0.1.0: el formato sigue aceptando los dos nombres
exactos del rescatador propio y verifica los hashes de sus copias antiguas. Al
restaurar se conservan los archivos actuales de Guardian: ambos nombres se
saltan en las escrituras, en la comparación de destinos y en el progreso de
restauración, aunque sus datos archivados siguen comprobándose. Se informa que
la instalación actual del rescatador se ha conservado.

Si un diario 0.1.0 ya movió uno de esos archivos a `restore/old`, dejó una copia
en `restore/new` o publicó su plan, y ahora falta su instalación de destino, la
recuperación se bloquea antes de modificar otros destinos o limpiar temporales.
Se pide reinstalar el rescatador actualizado y se conserva su copia protegida.
Una vez reinstalado, se puede continuar sin restaurar el ejecutable antiguo.
Un archivo actual vacío o especial tampoco se considera una instalación que
permita desechar esa protección. La comprobación no demuestra por sí sola la
versión del binario; el instalador debe proporcionar el rescatador actualizado.

## Pruebas

`tests/run-core-tests.ps1` compila un adaptador host aislado y pruebas reales de
archivos en una carpeta temporal. Incluye vectores SHA-256 conocidos,
estimación sin escritura, exclusiones, CID desconocido/distinto, corrupción,
fallos de escritura, interrupciones de promoción/restauración y rechazo de un
manifiesto manipulado con su sello recalculado. El adaptador simula una entrada
especial para no requerir privilegio de enlaces simbólicos de Windows.

La regresión de 0.1.1 construye un respaldo antiguo sintético con los dos
recursos de Guardian 0.1.0, instala archivos distintos que representan 0.1.1 y
restaura el entorno conservando esos archivos nuevos. También simula el diario
antiguo con el rescatador apartado y comprueba que se conserva el rollback
hasta reinstalar, y que una copia archivada dañada sigue bloqueando la operación.
No se usan respaldos reales ni CIDs de usuarios como fixtures de pruebas.

Estas pruebas no sustituyen pruebas físicas en V1/V2/Lite/OLED ni demuestran
durabilidad del hardware frente a desconexiones eléctricas reales.

Para diagnosticar una copia física sin modificarla:

```
./tests/inspect-backup.ps1 -SdRoot C:/ruta/copia-raiz-SD
./tests/inspect-backup.ps1 -SdRoot C:/ruta/copia-raiz-SD -RescueFormatter
```

La raíz indicada debe contener `config/NinMagicGuardian/backup`. El diagnóstico
imprime presencia, tipo y tamaño de las carpetas/índice/sello/marcadores, el
mensaje exacto de `gc_inspect`, el CID guardado en el manifiesto y la verificación
de todos sus datos. La segunda variante usa el formateador del rescatador para
comprobar lectura cruzada de una copia generada por el NRO. No hay funciones de
escritura disponibles en su contexto. Un CID guardado se puede leer sin conocer
el CID actual; comparar el CID físico corresponde después al adaptador. Así no
se confunde un índice ilegible o inválido con una identidad de tarjeta distinta.

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
se retira solo cuando todas las entradas han vuelto o termina una eliminación
expresamente confirmada. Las carpetas vacías de
aislamiento pueden permanecer y no afectan al arranque. La integridad por SHA-256
no es una firma ni protege contra alguien que edite manualmente los registros
y recalcule sus hashes con acceso completo a la SD.

Las pruebas host incluyen subconjuntos válidos, selecciones inventadas/caducadas,
marcador vacío con ejecutable cambiado, CID distinto, plan interrumpido,
manipulación de archivos apartados, colisiones del destino, carpetas en lugar
del inventario y reactivación sin el sello del respaldo actual. También comprueban
que una ruta lógica larga del tema pueda moverse a la carpeta interna y volver.

## Recuperación integrada de archivos y extras confirmados (0.2.0)

El NRO y el rescatador comparten `gr_restore` de `guardian_recovery.h`. La UI
obtiene una revisión de `ga_inspect`, muestra los candidatos y permite confirmar
el apartado de todos o de una selección. Esos candidatos solo son módulos con
`boot2.flag` y el tema de HOME definido anteriormente; no se interpreta cualquier
archivo nuevo como un fallo ni se amplía la acción a KIPs, juegos o carpetas
arbitrarias. La UI debe explicar que los cambios no demuestran cuál causó el
problema. También existe la elección explícita «solo archivos»; sin inventario
comparable solo se ofrece esa elección con su aviso, nunca un apartado automático.

1. `gc_restore_preflight` comprueba en modo de solo lectura el respaldo completo,
   CID, diario pendiente y recursos propios protegidos. No inspecciona ni
   modifica archivos de `switch`. Una sustitución de respaldo pendiente
   debe terminarse desde la acción separada de recuperación del respaldo.
2. Si se confirmó un apartado, `ga_quarantine` publica el diario íntegro y mueve
   únicamente las entradas confirmadas, conservándolas para reactivación.
   Si ya había un plan, `ga_pending_covers` exige que los candidatos mostrados
   pertenezcan a ese mismo plan (tipo, ID, huella y respaldo). No se mezcla una
   selección distinta con el plan anterior.
3. Solo cuando el apartado termina correctamente se llama a `gc_restore`, que
   vuelve a verificar los datos y recupera exclusivamente los archivos del entorno cubiertos.

Si falla el apartado, no comienza la restauración de archivos. Si la restauración
se interrumpe después, se informa de que los extras siguen apartados y se conservan
ambos diarios para reintentar. Una duda al leer el plan no se trata como ausencia:
se bloquea la acción y se conserva el estado. Un reintento continúa el mismo plan
confirmado y el diario de archivos. El resultado expone los cambios de la llamada
y si sigue existiendo un aislamiento; no afirma haber detectado la causa del fallo.
La elección explícita de solo archivos no aparta complementos nuevos y el mensaje
lo recuerda. El aislamiento previo permanece reversible mientras no se haya publicado una
intención de eliminación permanente. En ese caso solo se permite continuar el
plan completo ya confirmado o cancelar.

`tests/recovery_test.c` comprueba el orden real apartado→archivos, reposición de
archivos del entorno, rechazo de huellas caducadas, identidad distinta y errores
de lectura sin cambios, interrupciones en ambas fases y su reanudación, cobertura
de un plan anterior y archivos nuevos de juegos/herramientas conservados por hash.

La carpeta `switch` no se copia, analiza ni restaura en ninguna de estas acciones.
Las pruebas usan archivos con extensión `.nro` como datos opacos y comprueban que
los añadidos después de una copia permanecen idénticos por SHA-256. Si se borra
un NRO excluido, restaurar el entorno no lo recrea. Las opciones siguen limitadas
a 0..3 y el único formato de manifiesto aceptado es `NMGUARDIAN1`.


### Eliminación permanente expresamente confirmada

`gr_restore_mode(ctx, revision, 1)` exige una confirmación adicional e irreversible.
Primero aparta la selección, después restaura y verifica los archivos del entorno
y **solo al terminar correctamente** llama a `ga_discard_quarantine`. Si hay un
plan previo, la revisión debe contener **todas** sus entradas exactas, sin omisiones,
duplicados ni ampliaciones. `NULL` con modo permanente es un error. `gr_restore`
conserva el comportamiento temporal y reversible.

Antes de borrar el primer archivo, `discard.txt` se publica con sincronización,
sello propio, CID y SHA-256 del plan exacto. Enumera los archivos por ruta,
tamaño y SHA-256, y también cada directorio exacto del tema. Solo admite rutas
derivadas de las entradas confirmadas dentro de `quarantine/active/data`:
`<ID>/flags/boot2.flag` y `0100000000001000/romfs/...`. No admite rutas prestadas,
traversal, enlaces, archivos especiales, duplicados ni recorridos de borrado libres.
Se revisan todos los datos restantes antes de continuar y cada archivo justo
antes de borrarlo. Los directorios se quitan exclusivamente si están registrados
y vacíos; se rechazan archivos o carpetas nuevas que aparezcan en el apartado.

Los archivos ausentes en un reintento se consideran pasos ya completados de esa
misma intención. No se exige que una carpeta parcialmente eliminada siga teniendo
la huella original completa: la lista sellada de archivos y directorios permite
terminar sin ampliar el alcance. Un origen activo que reaparezca bloquea la
operación y se conserva. La intención es irreversible: mientras exista no se
puede reactivar, ampliar el plan ni cambiar a una restauración temporal o solo
de archivos desde el coordinador integrado.

Al desaparecer todo el contenido confirmado se publica `discard.done`, ligado al
CID, al plan y a la intención. Después se retira `discard.txt` y **el plan se
retira en último lugar**. El pequeño sello final no contiene datos para reactivar
complementos y permite reconocer un cierre interrumpido. No impide crear un
respaldo/inventario nuevo; el siguiente apartado elimina únicamente ese sello
completado. Una intención huérfana o ilegible bloquea la sustitución del respaldo
y del inventario. No se borra su registro para ocultar la incertidumbre.

`ga_pending_review` es de solo lectura, devuelve `0` o `-1` y muestra siempre las
entradas del plan original, incluso tras una eliminación parcial. Sus indicadores
`quarantine_pending` y `discard_pending` permiten explicar en pantalla qué está
pendiente. Las tres elecciones visibles son solo archivos, apartado temporal
recomendado y eliminación definitiva con otra confirmación. El usuario revisa la
lista: nuevo/cambiado no significa que se haya probado culpabilidad.

La eliminación de un módulo quita solo su marcador de arranque, no su ejecutable
ni su carpeta completa. La del tema quita solo el `romfs` apartado de HOME.
Juegos, sus mods, cheats, partidas y toda la carpeta `switch` permanecen excluidos.
No se promete reparar cualquier causa de fallo de arranque.

`tests/discard_test.c` inyecta cortes después de cada archivo y directorio del
apartado, y durante intención/cierre/retirada de registros; también comprueba
fallos de borrado, selecciones incompletas o duplicadas, datos modificados,
archivos y carpetas nuevos, objetos especiales, CID distinto, rutas prestadas,
plan huérfano, orden restauración antes de borrado, reintentos parciales y una nueva
copia e inventario tras completar la eliminación. Los NRO y archivos nuevos de
juegos se usan como datos opacos para comprobar que siguen intactos.
