# Cambios

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
