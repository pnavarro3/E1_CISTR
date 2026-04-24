## Resumen funcional
- Modulo/archivo analizado: main/main.c
- Objetivo funcional detectado: Gestionar la solicitud de carga de 3 materiales (Arena, Agua y Cemento) por botones GPIO, usando ISR y semaforos por material para controlar ocupacion de receptaculos.

## Relacion con el enunciado
- Requisito del enunciado: Existencia de 3 materiales diferenciados y su control de ingreso.
- Evidencia en codigo:
  - Definicion de materiales: main/main.c (materiales[]).
  - Asignacion de botones por material en GPIO18, GPIO19 y GPIO21: main/main.c (pines_boton[]).
- Estado: Cumple parcialmente (pendiente de confirmar redaccion exacta del requisito en doc/Entregable_1.pdf).

- Requisito del enunciado: Disparo de solicitud por evento de boton.
- Evidencia en codigo:
  - ISR registrada por pin: main/main.c (configurar_botones -> gpio_isr_handler_add).
  - Entrega de senal por ISR: main/main.c (isr_boton -> xSemaphoreGiveFromISR).
  - Interrupcion por flanco negativo: main/main.c (gpio_config_t.intr_type = GPIO_INTR_NEGEDGE).
- Estado: Cumple parcialmente (pendiente de confirmar condicion exacta pedida en el enunciado).

- Requisito del enunciado: Procesamiento concurrente y control de disponibilidad por receptaculo.
- Evidencia en codigo:
  - Creacion de semaforos por material: main/main.c (xSemaphoreCreateBinary para sem_espacio_libre y sem_solicitud).
  - Inicializacion de espacio libre: main/main.c (xSemaphoreGive(sem_espacio_libre[i])).
  - Tarea por material: main/main.c (xTaskCreate(tarea_carga_material, ...)).
  - Bloqueo por solicitud y espacio disponible: main/main.c (xSemaphoreTake en tarea_carga_material).
- Estado: Cumple parcialmente (pendiente de confirmar si el enunciado exige mas estados/transiciones).

- Requisito del enunciado: Trazabilidad/observabilidad del flujo.
- Evidencia en codigo:
  - Logs de aceptacion y ocupacion de receptaculo en tarea_carga_material.
  - Logs de inicializacion y mapeo de botones en app_main.
- Estado: Cumple parcialmente (pendiente de confirmar formato o eventos de log exigidos por el enunciado).

## Flujo del codigo
- Evento de entrada:
  - Pulsacion de boton asociado a un material en GPIO18/19/21.
- Procesamiento:
  - ISR isr_boton publica una solicitud en sem_solicitud[i].
  - La tarea tarea_carga_material correspondiente desbloquea por solicitud.
  - Si hay espacio libre en sem_espacio_libre[i], registra aceptacion y marca ocupacion (consumiendo el semaforo).
- Resultado esperado:
  - Se acepta la carga de un material solo cuando existe espacio libre para ese receptaculo.
  - El sistema mantiene una tarea concurrente por material y queda en ejecucion continua.

## Brechas detectadas
- Punto no cubierto:
  - No se observa en el codigo analizado la liberacion explicita del receptaculo (re-emision de sem_espacio_libre) luego de una etapa de "preparacion".
- Impacto:
  - Cada material podria quedar ocupado de forma indefinida despues de la primera carga aceptada.
- Nota de seguimiento:
  - Validar si esa liberacion ocurre en otro modulo o aun no esta implementada.

- Punto no cubierto:
  - No se observan validaciones de errores para creacion de semaforos/tareas ni para registro de ISR.
- Impacto:
  - Fallas de inicializacion pueden quedar silenciosas.
- Nota de seguimiento:
  - Confirmar si el enunciado exige manejo de errores o tolerancia a fallos.

## Pendientes de confirmar
- Dato pendiente:
  - Texto exacto de cada requisito del enunciado en doc/Entregable_1.pdf.
- Motivo:
  - Desde esta interfaz no se pudo extraer texto legible del PDF (contenido comprimido/binario).

- Dato pendiente:
  - Criterio exacto de cumplimiento (funcionalidades obligatorias, opcionales y casos de prueba esperados).
- Motivo:
  - El detalle normativo esta solo en PDF y requiere lectura directa del documento.
