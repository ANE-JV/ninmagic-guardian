# NinMagic Guardian 0.2.0 — beta

Respaldo selectivo de los archivos de Atmosphère, Hekate y configuración de
arranque que están en la microSD. Un NRO para usar cuando Atmosphère funciona
y un rescatador independiente para abrir desde Hekate cuando no funciona.

**Versión beta: compilada y comprobada con pruebas en PC. Todavía necesita
pruebas físicas en V1, V2, Lite y OLED.** No confundir esta copia de archivos
con un respaldo de NAND, del firmware de Nintendo o de toda la tarjeta.

## Instalar el ZIP

Extrae `NinMagic-Guardian-0.2.0.zip` en la raíz de la microSD. El ZIP también
se puede ofrecer en tu servidor de herramientas y extraer con NinMagic AIO.
No incluye ni reemplaza el pack completo de Atmosphère o Hekate.

**Si instalaste 0.1.0, actualiza el ZIP completo antes de volver a abrir su
rescatador.** La 0.1.1 corrige un bloqueo del arranque del rescatador y la
comparación de identidad de algunas microSD. Tu respaldo anterior se conserva
y sigue siendo verificable; no hace falta borrarlo ni crear uno nuevo.

La 0.1.3 corrige la pantalla del rescatador que mostraba «%u» en vez del
número de archivos y el tamaño, y podía dejar la fecha vacía. También ajusta
las líneas largas al área de pantalla. Requiere el ZIP completo para actualizar
el rescatador; conserva el respaldo existente.

Abre hbmenu y **NinMagic Guardian**. El icono GUARDIAN utiliza el logo NinMagic
con azul eléctrico, verde neón y ámbar para distinguirlo de las otras apps.

## Primer recorrido

1. Comprueba que la consola arranca y que todo funciona bien.
2. Abre Guardian y elige **Crear / actualizar respaldo**. Verás el número de
   archivos y el tamaño calculado antes de confirmar **Todo funciona bien**.
3. Espera al resultado de la copia y de la verificación. La copia anterior
   se conserva hasta verificar y promover la nueva. Hace falta espacio
   temporal para ambas generaciones; después queda un respaldo final.
4. Elige **Preparar rescate en Hekate** para comprobar o actualizar sus recursos
   independientes. Si falta o difiere algún recurso, se prepara con verificación
   y diario de reintento. El respaldo no se modifica durante este paso.
5. Reinicia a Hekate y abre **More configs > NinMagic Guardian - Rescate SD**.
   La primera vez usa únicamente **Comprobar respaldo**. VOL+/VOL- seleccionan
   y POWER confirma. La restauración requiere otra confirmación.
   Son los botones físicos de volumen y encendido de la consola, no los botones
   del mando. En resultados, cualquiera de esos tres botones vuelve al menú.
   El menú incluye **Salir / reiniciar consola** y **Salir / apagar consola**.

Guardian no crea automáticamente una copia nueva de un arranque desconocido.
Actualiza el respaldo cuando hayas comprobado que el estado nuevo funciona.

## Revisar temas y módulos

Al crear el respaldo se guarda también un inventario pequeño de huellas de
módulos de arranque y del tema HOME. No se copian sus datos al respaldo del
sistema. El inventario se vincula al respaldo exacto y a la misma microSD.

**Revisar temas y módulos** compara ese inventario con el estado actual.
Presenta complementos nuevos o que cambiaron: no demuestra que sean la causa
del fallo. Marca los que quieras con A y revisa con Y antes de confirmar.
En un módulo solo se aparta su marcador `boot2.flag`; para el tema HOME se
aparta su carpeta `romfs`. Los mods de los juegos quedan fuera de esta acción.

Los archivos apartados se conservan en la carpeta de recuperación de Guardian.
La opción **Recuperar apartados** permite devolverlos a su ruta si no está
ocupada; puede devolver también el fallo. No sobrescribe archivos nuevos que
otras aplicaciones hayan colocado en esas rutas. Estas acciones también se
ofrecen en el rescatador de Hekate, con revisión y confirmación.
En esta beta, mientras haya complementos apartados o un plan pendiente,
se conserva el respaldo existente y se bloquea crear uno nuevo. Revisa primero
la recuperación de esos complementos. Reactivarlos puede devolver el fallo.

## Después de probar el arranque

Si la recuperación temporal permite volver a iniciar Atmosphère, abre Guardian
> **Revisar temas y módulos**. El apartado anterior se muestra con su lista
completa. **R — Eliminar apartados** abre una confirmación: **X** elimina
permanentemente los elementos mostrados y **B** cancela. No se repite la
restauración de archivos. **X — Recuperar apartados** en la pantalla anterior
permite reactivarlos si aún se conservaron; puede devolver el fallo.

Una eliminación definitiva interrumpida solo puede continuar el mismo plan;
no se puede reactivar un tema parcialmente eliminado ni añadir otros candidatos.
Se muestra su lista original completa, incluidos los pasos ya terminados.
El diario solo elimina rutas propias enumeradas y verificadas. Al completar,
no quedan copias recuperables de esos elementos: solo un pequeño sello de
finalización, sin el contenido del tema o los marcadores.

## Qué se conserva y qué queda fuera

Los archivos esenciales reconocidos se seleccionan automáticamente. Los
fondos e imágenes del menú y otros payloads son extras opcionales.
La lista técnica exacta está en `docs/core-format.md` y en
`gc_managed_path()`; cualquier archivo fuera de ella queda excluido.

La carpeta `switch` completa, incluidos todos sus NRO de herramientas, juegos y
ports, queda siempre fuera del respaldo. Restaurar no borra sus archivos.

Los juegos instalados, juegos portados, partidas, mods, cheats, Android, Linux
y el contenido de la emuMMC no forman parte de esta copia y no se borran al
restaurar. No se abre ni se escribe NAND, BOOT0/BOOT1 o particiones.
La configuración de selección de emuMMC sí pertenece al arranque: restaurarla
vuelve a la selección guardada, sin volver atrás su contenido o juegos.

Un respaldo creado antes de instalar juegos no elimina esos juegos al
restaurarlo. Su funcionamiento sigue dependiendo de que el firmware, la
emuMMC y la instalación de los propios juegos estén en buen estado.

## Recuperar

Al elegir **Restaurar el arranque**, Guardian revisa también los temas y módulos
que cambiaron desde la copia. Si hay un inventario válido, quedan seleccionados
por defecto para apartarlos de forma reversible antes de reponer los archivos.
**X** permite ver y elegir los cambios; **Y** cambia entre apartado temporal,
eliminación definitiva y solo archivos. La eliminación requiere una pantalla
adicional con su lista y confirmación mediante **X**. Si no
hay inventario comparable, se avisa y no se aparta nada automáticamente.
No se demuestra que cada cambio sea culpable. El modo temporal conserva los
elementos apartados para reactivarlos o eliminarlos después; el modo definitivo
elimina ese contenido solo después de recuperar los archivos del arranque.
Si falla este apartado confirmado, no continúa la
restauración; si falla después la copia de archivos, se conservan los diarios
para reintentar. No se hace una limpieza general de archivos nuevos.

El rescatador ofrece la misma recuperación integrada y muestra primero los
candidatos y la opción que vas a confirmar. Los complementos se apartan
desactivando su marcador de inicio, o moviendo el tema HOME a recuperación.
Los juegos y sus mods no se mueven. Para un módulo se aparta o elimina su
marcador de inicio, sin borrar la carpeta completa del módulo. Para el tema HOME
se apartan o eliminan los archivos del tema seleccionado. Un archivo nuevo de otro tipo puede requerir
un diagnóstico distinto: Guardian no identifica cualquier causa de fallo.

Si abre Atmosphère, usa **Restaurar el arranque** desde Guardian. Si no abre,
usa el rescatador de Hekate y el mismo respaldo. Los archivos se verifican
con SHA-256 y la copia debe corresponder a la tarjeta física que está leyendo.

La restauración solo sustituye rutas incluidas en el respaldo. Los archivos
añadidos después y los plugins externos no se eliminan automáticamente.
Desde 0.1.1 también se conservan el rescatador Guardian y su recurso Minerva
actualmente instalados, aunque una copia antigua de 0.1.0 contenga los suyos.
Así una restauración del sistema no vuelve a instalar el rescatador anterior.
Un tema, módulo o mod externo que cause el fallo puede requerir desactivarse
por separado. Los cambios cubiertos por el inventario se revisan desde la
opción anterior; la app no puede atribuir con certeza un fallo a cualquier archivo.

Si se interrumpe una restauración, vuelve a **Restaurar** para continuar el
diario. No arranques Horizon hasta terminarla. No extraigas la SD ni apagues
mientras se escribe. El rescatador exige un mínimo de batería.

Una actualización de Nintendo puede requerir versiones nuevas compatibles de
Atmosphère/fusee/Hekate. Una copia antigua no crea esa compatibilidad ni
deshace el firmware. El NRO bloquea restaurar desde Horizon si su firmware
actual no coincide con el registrado; el rescatador no lee NAND para detectar
su firmware y muestra el límite antes de confirmar.

No repara averías físicas de microSD, lector o consola. Si Hekate tampoco
puede iniciar, será necesario recuperar primero el método de arranque de
esa consola. La compatibilidad del hardware de base no sustituye probar
este rescatador en cada modelo.

## Actualizaciones

Repositorio y canal independientes de la web y de las demás aplicaciones:
https://github.com/ANE-JV/ninmagic-guardian

Desde Guardian pulsa **R — Actualizar app**. La consulta HTTPS identifica la
versión publicada; la descarga comprueba nombre, versión, tamaño y SHA-256.
La instalación pide confirmación y conserva un NRO anterior. Cierra y vuelve
a abrir Guardian para usar la versión instalada.

El actualizador descarga y sustituye el NRO. Al volver a abrir la app, usa
**Preparar rescate en Hekate** para aplicar los recursos que lleva dentro.
El ZIP completo sigue disponible. Si se interrumpe esa preparación, vuelve al
NRO y repite **Preparar rescate** para continuar su diario. Los tres recursos
no se sustituyen simultáneamente: no apagues ni retires la tarjeta durante
esa operación. La configuración de canal inicial es:
`config/NinMagicGuardian/update-url.txt`. Si falta, se utiliza el canal oficial
incorporado; una configuración presente pero inválida se informa como error.

## Archivos propios

- `switch/NinMagicGuardian/NinMagic-Guardian.nro`
- `config/NinMagicGuardian/backup/` — copia verificada y diarios de recuperación
- `config/NinMagicGuardian/app-update/` — descargas y respaldo del NRO
- `config/NinMagicGuardian/rescue/` — payload y recurso Minerva independientes
- `config/NinMagicGuardian/rescue-update/` — diario y copias de la actualización del rescate
- `bootloader/ini/ninmagic-guardian.ini` — acceso propio en More configs

No modifica `bootloader/hekate_ipl.ini` al instalar el acceso. Si `bootprotect`
impide crear el acceso desde Horizon, extrae el ZIP completo en el PC.

## Compilar y licencias

NRO: devkitA64, libnx, portlibs curl y zlib de devkitPro. Ejecuta
`powershell -File tools/build.ps1`. Rescatador: devkitARM y Python,
`powershell -File tools/build-rescue.ps1`; compílalo antes del NRO para incluir
sus recursos. `python tools/package.py` prepara los archivos de distribución.

GPL-2.0-only. El rescatador deriva de Hekate y conserva los avisos upstream.
Las fuentes completas correspondientes se ofrecen junto al ZIP, incluyendo
el archivo de fuentes del rescatador. Consulta `LICENSE`, `third_party/` y
`rescue/README.md`. NinMagic Guardian no es una publicación oficial de los
autores de Atmosphère, Hekate o Nintendo.
