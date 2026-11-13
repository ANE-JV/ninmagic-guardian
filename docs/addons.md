# Inventario y apartado de complementos de arranque

Este complemento comparte el adaptador SD y SHA-256 del respaldo. No abre NAND,
particiones ni partidas. Es independiente de la restauración de archivos del
arranque: restaurar el respaldo no aparta complementos por sí solo.

`ga_capture()` se llama después de crear y verificar un respaldo confirmado por
el usuario. Guarda huellas y presencia, no el contenido de mods o cheats. La
referencia se vincula al SHA-256 del manifiesto exacto y a la identidad física
de la tarjeta.

Se examinan:

- Carpetas con Title ID hexadecimal de 16 caracteres en `atmosphere/contents`
  que tengan el marcador `flags/boot2.flag`. La huella contempla el marcador y
  ejecutables conocidos del módulo: `exefs.nsp`, `exefs/rtld`, `exefs/main` y
  `exefs/main.npdm`.
- El árbol `atmosphere/contents/0100000000001000/romfs`, correspondiente al tema
  del menú HOME. Se calculan huellas de sus archivos normales.

Los cambios de un mod de un juego que no constituya un complemento de arranque
quedan fuera. El inventario tiene límites de tamaño, profundidad y cantidad;
si se superan, informa el error y no declara una revisión completa.

`ga_inspect()` solo lee. Identifica hasta 32 candidatos nuevos o cambiados.
Un candidato no es una causa demostrada del fallo y la ausencia de candidatos
no demuestra que el sistema esté sano. La app permite marcar un subconjunto,
desmarcado por defecto, y exige una pantalla de confirmación. El rescatador
ofrece la misma selección y confirmación.

`ga_quarantine()` vuelve a verificar la identidad, el respaldo y las huellas
de la selección antes de mover. En un módulo mueve únicamente `boot2.flag`;
en el tema mueve únicamente la carpeta fija `romfs`. Los archivos quedan en
`config/NinMagicGuardian/quarantine/active/data/` con un plan que permite
continuar una operación interrumpida. No se sobrescribe un destino ocupado.

`ga_restore_quarantine()` requiere la misma tarjeta y un plan íntegro. Revisa
las huellas del contenido y de los ejecutables que correspondan antes de
reactivar. No depende de que siga existiendo el respaldo de sistema original,
pero no pisa archivos nuevos, enlaces o destinos ambiguos. Un cambio externo
o una colisión detienen la acción conservando los datos para revisión.

Para evitar renovar la referencia mientras exista una operación de aislamiento,
`gc_create_backup()` y `ga_capture()` bloquean un plan de apartados presente.
Esta beta conserva un solo plan de aislamiento a la vez. Reactivar un
complemento puede devolver el problema; los cambios de arranque requieren
reiniciar para comprobar su efecto.

Las pruebas host inyectan interrupciones y alteraciones, prueban subconjuntos,
revisiones caducadas o manipuladas, CID distinto, planes activos y conflictos de
reactivación. La suite también se ejecuta con el formateador exacto del payload.
Esto no sustituye pruebas físicas ni demuestra que cualquier fallo provenga de
un tema o módulo detectado.
