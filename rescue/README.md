# NinMagic Guardian: rescate antes de Atmosphere

Componente independiente derivado de Hekate, licencia GPL-2.0-only, fuentes
oficiales fijadas en `8e1392df4569ac501a115867056b13c361429e9e`.
No es una version oficial de Hekate ni esta respaldado por sus autores.

El payload arranca directamente a Guardian. Solo expone FatFs sobre la microSD;
los controladores y herramientas eMMC/emuMMC, Horizon, UMS, L4T y Nyx no estan
enlazados. La inicializacion hardware y DRAM procede del Hekate fijado, que
contempla Erista y Mariko. Este fork NO se ha probado aun en V1, V2, Lite ni OLED.
Su primera prueba fisica debe empezar por la verificacion de solo lectura.

## Instalacion separada

- `config/NinMagicGuardian/rescue/ninmagic-guardian-rescue.bin`
- `config/NinMagicGuardian/rescue/libsys_minerva.bso`
- `bootloader/ini/ninmagic-guardian.ini`

Se abre desde Hekate, **More configs -> NinMagic Guardian - Rescate SD**.
No sustituye `hekate_ipl.ini`, el payload principal ni los recursos de Hekate.
Si el Hekate principal no inicia, este payload puede elegirse/injectarse como
un payload de rescate mediante el metodo compatible con esa consola. No repara
un lector SD averiado ni una tarjeta que no se puede leer.

Controles: VOL +/- mueve la seleccion y POWER confirma. En pantallas de resultado
o error, VOL +/- o POWER regresa al menu; el pie aparece en una posicion fija.
La restauracion exige otra confirmacion. El menu ofrece salir apagando o
reiniciando la consola. Reiniciar usa su metodo habitual de arranque; puede
necesitar su payload y no garantiza regresar a Hekate.

## Respaldo y limites

El mismo nucleo de archivos que usa el NRO verifica el manifiesto y todos los
hashes, comprueba la identidad fisica de la SD y restaura unicamente la lista
permitida del respaldo. No monta NAND, no modifica particiones ni borra juegos,
partidas, Android, Linux, cheats o mods. No diagnostica automaticamente la causa
exacta de cada fallo; restaura los archivos de arranque cubiertos por la copia.
Los archivos nuevos ajenos a esa lista permanecen. Otra opcion compara los
modulos de arranque y el tema de HOME con el inventario creado por el NRO tras
el respaldo. Muestra candidatos nuevos/cambiados, sin asegurar que sean la
causa: el usuario elige cuales apartar y confirma. Solo mueve `boot2.flag` de
los modulos y el `romfs` del tema HOME fijo; los mods de juegos permanecen.
Se pueden reactivar los elementos apartados, sin sustituir destinos existentes.

La identidad del NRO es el CID crudo de 16 bytes que devuelve libnx, expresado
en 32 digitos hexadecimales minusculos. La 0.1.1 invierte exactamente el
desplazamiento de ocho bits que Hekate aplica a los cuatro registros SDHCI R2,
para obtener la representacion de GetResponse/SetCid/GetCid del SDK. Compara
los 16 bytes completos; no ignora ni enmascara campos. Tambien conserva las
comparaciones exactas por orden byte/palabra para formatos anteriores.
Las diferencias entre respaldo ausente, lectura/manifiesto danado y CID distinto
se muestran por separado, con el mensaje del motor y diagnostico de FatFs/CID.

La entrada 0.1.1 cambia la pila exclusivamente en un trampoline ensamblador,
antes de entrar a una funcion C con variables locales. Reserva 56 KiB de IRAM
entre 0x40031000 y 0x4003F000, separado del payload, parametros DRAM y pilas de
interrupciones. `check_entry.py` valida tanto la fuente como el ELF enlazado;
la compilacion genera el informe de GCC `stack-usage.json`.

El rescate no conoce el firmware instalado en NAND. Antes de confirmar muestra
el firmware anotado en la copia: restaurar un Atmosphere antiguo tras actualizar
el firmware puede no arrancar. Confirme solo si la copia sigue siendo compatible.

Una copia interrumpida conserva la anterior hasta que el nucleo confirma la
nueva; una restauracion interrumpida tiene diario y puede continuarse. Esto
reduce los riesgos, pero no garantiza supervivencia a un fallo fisico de SD.
No retire la tarjeta ni apague mientras se escribe. Para restaurar se requiere
20 % de bateria y el recurso Minerva de esta misma distribucion.

## Compilar y distribuir

En Windows con Python 3, devkitARM y MSYS2 de devkitPro:

    powershell -File tools/build-rescue.ps1

Se descarga el commit oficial y se crea una carpeta de construccion exclusiva
en `%USERPROFILE%/.codex/tmp/guardian-rescue-build`. La instalacion principal y
la consola no se modifican. Salida en `dist/rescue`. El archivo
`dist/NinMagic-Guardian-Rescue-source.tar.gz` contiene las fuentes exactas con
las modificaciones y Makefile. Debe ofrecerse junto al binario para cumplir GPL.
Conserve los avisos de copyright y la licencia upstream incluidos.

La compilacion rechaza payloads que superen los limites de IRAM de Hekate.
La compilacion correcta y los analisis de simbolos no sustituyen la prueba
fisica de restauracion y corte de energia controlado en cada modelo.
