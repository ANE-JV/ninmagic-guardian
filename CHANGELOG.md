# Cambios

## 0.2.0 — Restauración con revisión del arranque

- Integra la revisión de temas y módulos en Restaurar: muestra los cambios,
  permite elegirlos y los aparta de forma reversible antes de recuperar archivos.
- Comprueba copia, identidad SD y diario antes de apartar; reanuda operaciones
  interrumpidas y conserva los planes y avisos si una fase no termina.
- Permite restaurar solo archivos con aviso explícito; sin inventario comparable
  no aparta complementos automáticamente.
- Añade eliminación definitiva explícita de los elementos apartados, después
  de recuperar los archivos o tras probar el arranque. Lista completa, segunda
  confirmación, intención sellada, hashes y reanudación limitada al mismo plan.
- Mantiene el apartado temporal recomendado para probar sin adivinar el culpable;
  una eliminación interrumpida no permite reactivar contenido parcial.
- Actualiza los tres recursos propios del rescatador desde la aplicación con
  verificación, copias anteriores y diario, sin cambiar el arranque principal.
- Mantiene compatibilidad con respaldos anteriores. La carpeta switch completa
  sigue excluida: no se respaldan ni se borran sus NRO, juegos, ports o datos.

Al actualizar desde la aplicación, vuelve a abrirla y elige Preparar rescate en
Hekate para aplicar también el rescatador incluido. Actualizar no crea un
respaldo nuevo ni restaura archivos de forma automática.

## 0.1.3 — Datos del respaldo en el rescatador

- Corrige la presentación del número de archivos, tamaño y fecha del respaldo
  en el rescatador. La verificación ya terminaba, pero los números aparecían
  como «%u» y podían desplazar el texto de la fecha.
- Unifica el formato de texto de las pantallas del rescatador y comprueba
  los mensajes con sus argumentos completos.
- Ajusta y limita las líneas de texto al área de pantalla disponible.
- Conserva el respaldo existente y las correcciones de arranque de 0.1.1.

Instala el ZIP completo para actualizar también el rescatador de Hekate.
Actualizar solo el NRO no sustituye el rescatador.

## 0.1.1 — Corrección del rescatador

- Corrige el arranque del rescatador que podía bloquearse y dejar de responder
  después de entrar en una operación.
- Reconoce la representación de identidad SD utilizada por Horizon/Atmosphère,
  manteniendo la comprobación de la misma tarjeta y los hashes del respaldo.
- Distingue respaldo ausente, error de lectura, corrupción e identidad distinta.
- Añade salida para reiniciar y apagar, y retorno al menú con cualquiera de
  los botones físicos de volumen o encendido desde los resultados.
- Conserva el rescatador actualizado al restaurar copias antiguas de 0.1.0.
- Incluye diagnóstico de solo lectura y lectura MTP para comprobar copias reales.

Para corregir el rescate de 0.1.0 hay que instalar el ZIP completo, no solo el
NRO. El respaldo anterior no se borra ni se modifica por esta actualización.

## 0.1.0 — Primera beta

- NRO con logo GUARDIAN y paleta azul eléctrico, verde neón y ámbar.
- Respaldo selectivo de archivos de arranque, cálculo previo de tamaño y
  sustitución del respaldo anterior después de verificar el nuevo.
- Restauración con lista limitada, identidad SD, huellas y diario de reintento.
- Rescatador preboot independiente basado en Hekate, con recursos propios.
- Inventario de temas/módulos y apartado reversible tras selección confirmada.
- Canal de actualización propio en GitHub, descarga verificada y recuperación
  del NRO anterior. El rescatador se distribuye en el ZIP completo.
- Paquete listo para extraer en la raíz de la microSD y fuentes correspondientes.

Probado con archivos y fallos simulados en PC. Pendiente de prueba física en
Switch V1, V2, Lite y OLED. No restaura firmware ni NAND y no garantiza reparar
errores fuera de los archivos e inventario cubiertos.
