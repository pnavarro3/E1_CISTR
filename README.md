# CISTR — Sistema de Mezcla de Materiales con ESP32

![ESP-IDF](https://img.shields.io/badge/ESP--IDF-v5.5.3-red?logo=espressif)
![FreeRTOS](https://img.shields.io/badge/FreeRTOS-embedded-green)
![CMake](https://img.shields.io/badge/CMake-3.16+-blue?logo=cmake)
![Lenguaje](https://img.shields.io/badge/Lenguaje-C-lightgrey?logo=c)
![Target](https://img.shields.io/badge/Target-ESP32-orange?logo=espressif)
![Flash](https://img.shields.io/badge/Flash-UART-yellow)

Sistema embebido desarrollado en **C** sobre **ESP32** con **ESP-IDF v5.5.3** y **FreeRTOS**. Simula una línea de producción para la mezcla de tres materiales (Arena, Agua y Cemento). El ingreso de cada material se activa mediante botones físicos que disparan interrupciones hardware; el procesamiento completo se gestiona a través de un pipeline de 9 tareas FreeRTOS concurrentes que cubren las etapas de Carga, Preparación, Enrutamiento y Mezcla.

> **Nombre del proyecto CMake:** `Entregable1` · **TAG de log:** `entregable1`

---

## Tabla de contenidos

1. [Características](#características)
2. [Estructura del repositorio](#estructura-del-repositorio)
3. [Arquitectura de colas](#arquitectura-de-colas)
4. [Requisitos](#requisitos)
5. [Instalación y uso](#instalación-y-uso)
6. [Configuración](#configuración)
7. [Pines GPIO](#pines-gpio)
8. [Flujo de ejecución](#flujo-de-ejecución)
9. [Lógica de combinación de materiales](#lógica-de-combinación-de-materiales)
10. [Tareas FreeRTOS](#tareas-freertos)
11. [Sincronización y sección crítica](#sincronización-y-sección-crítica)
12. [Salida de log esperada](#salida-de-log-esperada)
13. [Troubleshooting](#troubleshooting)
14. [Estado del proyecto](#estado-del-proyecto)
15. [Documentación adicional](#documentación-adicional)

---

## Características

- **Entrada por interrupción hardware (ISR):** cada botón físico dispara `isr_boton` por flanco negativo (`GPIO_INTR_NEGEDGE`) con filtro antirrebote de **1 000 ms** implementado en software.
- **Pipeline FreeRTOS de 4 etapas:** Carga → Preparación → Procesamiento → Estación de Mezcla, totalmente desacoplado mediante colas (`QueueHandle_t`).
- **9 tareas concurrentes:** 3 de carga (una por material) + 1 de preparación + 1 de procesamiento + 3 de estación + 1 de panel de estado.
- **3 estaciones de mezcla independientes y concurrentes**, cada una con un tiempo de procesamiento fijo de **10 segundos**.
- **Control de backpressure:** `tarea_preparacion` sondea activamente `cola_packs_preparados` y espera en bucle de 100 ms cuando la cola está al máximo (3/3).
- **Panel de estado periódico:** volcado del estado completo del sistema cada **2 segundos** vía `ESP_LOGI`.
- **Contador global de mezclas** `total_mezclas` protegido con sección crítica `portMUX_TYPE` (`portENTER_CRITICAL` / `portEXIT_CRITICAL`).
- **Receptáculo exclusivo por material:** `cola_receptaculos[i]` de capacidad 1 garantiza que cada material solo puede estar en carga una vez; pulsaciones adicionales se descartan con advertencia `ESP_LOGW`.

---

## Estructura del repositorio

```
CISTR/
├── main/
│   ├── main.c                          # Código fuente completo del sistema (único archivo C)
│   └── CMakeLists.txt                  # Registro del componente ESP-IDF
├── doc/
│   ├── Entregable_1.pdf                # Enunciado oficial del entregable
│   └── documentacion_funcional_main.md # Análisis funcional del código (versión parcial, ver nota)
├── build/                              # Artefactos de compilación (generado por idf.py build)
├── .vscode/
│   ├── settings.json                   # Ruta ESP-IDF, Python, puerto COM, target y OpenOCD
│   ├── launch.json                     # Configuraciones de depuración GDB y ESP-IDF
│   └── c_cpp_properties.json           # IntelliSense C/C++ con toolchain xtensa-esp32-elf
├── .devcontainer/                      # Configuración de contenedor de desarrollo (Pendiente de confirmar)
├── .github/
│   └── agents/                         # Agentes de automatización del repositorio
├── .clangd                             # Configuración del servidor de lenguaje clangd
├── CMakeLists.txt                      # CMake raíz: cmake ≥ 3.16, nombre proyecto Entregable1
├── sdkconfig                           # Configuración de ESP-IDF generada (target: esp32)
└── README.md                           # Este archivo
```

---

## Arquitectura de colas

El sistema utiliza exclusivamente `QueueHandle_t` de FreeRTOS para la comunicación entre tareas e ISR. No se usan semáforos en la implementación actual.

| Cola | Capacidad | Tipo de elemento | Propósito |
|---|---|---|---|
| `cola_pulsaciones[i]` | **1** | `uint8_t` | ISR → `tarea_carga_material[i]`: señal de pulsación por material |
| `cola_receptaculos[i]` | **1** | `int` | Ocupa / libera el receptáculo exclusivo del material `i` |
| `cola_materiales_disponibles` | **3** | `int` | `tarea_carga_material[i]` → `tarea_preparacion`: ID de material listo |
| `cola_packs_preparados` | **3** | `char` | `tarea_preparacion` → `tarea_procesamiento`: código de pack |
| `cola_estacion[i]` | **3** | `char` | `tarea_procesamiento` → `tarea_estacion[i]`: pack a mezclar |

**Total de colas instanciadas:** 12 (`cola_pulsaciones`×3 + `cola_receptaculos`×3 + 1 + 1 + `cola_estacion`×3)

---

## Requisitos

| Herramienta | Versión | Notas |
|---|---|---|
| [ESP-IDF](https://docs.espressif.com/projects/esp-idf/en/stable/esp32/) | **5.5.3** | Variable de entorno `IDF_PATH` obligatoria · configurada en `.vscode/settings.json` |
| CMake | **≥ 3.16** | Declarado en `CMakeLists.txt` raíz · incluido en la cadena de herramientas de ESP-IDF |
| Python | **3.11.2** | Ruta verificada en `.vscode/settings.json` → `idf.pythonInstallPath` |
| Compilador | **xtensa-esp32-elf-gcc** (esp-14.2.0_20251107) | Declarado en `.vscode/c_cpp_properties.json` |
| Hardware | **ESP32** (cualquier módulo compatible) | Configuración OpenOCD para ESP32-WROVER-KIT 3.3 V |
| VS Code + extensión ESP-IDF | Opcional | Recomendado para compilar, flashear y depurar desde el IDE |

---

## Instalación y uso

### 1. Clonar el repositorio

```bash
git clone <url-del-repositorio>
cd CISTR
```

### 2. Exportar el entorno de ESP-IDF

```bash
# Linux / macOS
. $HOME/esp/v5.5.3/esp-idf/export.sh

# Windows (PowerShell)
. C:\Users\<usuario>\esp\v5.5.3\esp-idf\export.ps1
```

> La variable `IDF_PATH` debe apuntar a la raíz de ESP-IDF. Sin ella, `idf.py` y CMake fallan.

### 3. Compilar

```bash
idf.py build
```

Los artefactos se generan en `build/`. El binario principal es `build/Entregable1.bin`.

### 4. Flashear en el dispositivo

```bash
# Reemplazar COMx por el puerto serie real (ej. COM8 en Windows, /dev/ttyUSB0 en Linux)
idf.py -p COMx flash
```

### 5. Monitorear la salida serie

```bash
idf.py -p COMx monitor
```

> **Atajo combinado:** `idf.py -p COMx flash monitor` — flashea y abre el monitor en un solo paso. El baudrate por defecto de ESP-IDF es **115 200 bps**.

### 6. Depurar con OpenOCD (opcional)

La configuración de depuración está en `.vscode/launch.json`. Soporta dos modos:

- **`Eclipse CDT GDB Adapter`** — depuración vía adaptador GDB remoto.
- **`Launch`** — integración directa con la extensión ESP-IDF de VS Code.

La configuración OpenOCD activa es `board/esp32-wrover-kit-3.3v.cfg` (definida en `.vscode/settings.json`).

---

## Configuración

### Constantes del firmware (`main/main.c`)

| Constante / expresión | Valor | Descripción |
|---|---|---|
| `TIEMPO_ANTIRREBOTE` | `pdMS_TO_TICKS(1000)` → **1 000 ms** | Ventana de supresión de rebotes en ISR |
| `NUM_MATERIALES` | `3` | Número de materiales: Arena, Agua, Cemento |
| `CAPACIDAD_RECEPTACULO` | `1` | Capacidad de `cola_receptaculos[i]` (un material por vez) |
| `CAPACIDAD_PACKS` | `3` | Capacidad de `cola_packs_preparados` y `cola_estacion[i]` |
| `vTaskDelay` en `tarea_estacion` | `pdMS_TO_TICKS(10000)` → **10 000 ms** | Duración fija de cada mezcla |
| `vTaskDelay` en `tarea_panel_estado` | `pdMS_TO_TICKS(2000)` → **2 000 ms** | Frecuencia del volcado de estado |
| Sondeo de backpressure | `pdMS_TO_TICKS(100)` → **100 ms** | Periodo de espera activa cuando `cola_packs_preparados` está llena |

### Configuración del entorno de desarrollo (`.vscode/settings.json`)

| Parámetro | Valor en el workspace original |
|---|---|
| `idf.espIdfPathWin` | `C:\Users\Pablo\esp\v5.5.3\esp-idf` |
| `idf.pythonInstallPath` | `C:\Users\Pablo\.espressif\tools\idf-python\3.11.2\python.exe` |
| `idf.toolsPathWin` | `C:\Users\Pablo\.espressif` |
| `IDF_TARGET` (customExtraVars) | `esp32` |
| `idf.portWin` | `COM8` |
| `idf.flashType` | `UART` |
| `idf.openOcdConfigs` | `board/esp32-wrover-kit-3.3v.cfg` |

> ⚠️ Estas rutas son específicas del entorno de desarrollo original. Cada colaborador debe ajustarlas a su instalación local o configurarlas mediante la extensión ESP-IDF de VS Code.

---

## Pines GPIO

| Material | ID interno | GPIO | Pull-up interno | Tipo de interrupción |
|---|---|---|---|---|
| Arena | `0` | **GPIO_NUM_18** | Habilitado (`GPIO_PULLUP_ENABLE`) | Flanco negativo (`GPIO_INTR_NEGEDGE`) |
| Agua | `1` | **GPIO_NUM_19** | Habilitado (`GPIO_PULLUP_ENABLE`) | Flanco negativo (`GPIO_INTR_NEGEDGE`) |
| Cemento | `2` | **GPIO_NUM_21** | Habilitado (`GPIO_PULLUP_ENABLE`) | Flanco negativo (`GPIO_INTR_NEGEDGE`) |

Los tres pines se configuran en bloque mediante una sola llamada a `gpio_config()`. El servicio ISR se instala con `gpio_install_isr_service(0)` y cada handler se registra con `gpio_isr_handler_add()`.

---

## Flujo de ejecución

### Secuencia de inicialización en `app_main`

```
1. Crear cola_pulsaciones[i], cola_receptaculos[i] y lanzar tarea_carga_material[i]   (×3)
2. Crear cola_materiales_disponibles  (cap. NUM_MATERIALES = 3)
3. Crear cola_packs_preparados        (cap. CAPACIDAD_PACKS  = 3)
4. Crear cola_estacion[i]             (cap. CAPACIDAD_PACKS  = 3, ×3)
5. Lanzar tarea_preparacion
6. Lanzar tarea_procesamiento
7. Lanzar tarea_panel_estado
8. Lanzar tarea_estacion[i]           (×3)
9. configurar_botones()
   └─ gpio_config() → gpio_install_isr_service(0) → gpio_isr_handler_add() (×3)
10. ESP_LOGI de confirmación de inicio
11. Bucle infinito de app_main con vTaskDelay(1 000 ms)
```

### Pipeline en régimen estacionario

```
[Botón físico]
      │  flanco negativo
      ▼
  isr_boton(i)
  ├─ Δt < 1 000 ms → descarta (antirrebote)
  └─ xQueueSendFromISR → cola_pulsaciones[i]  (cap. 1)
       └─ portYIELD_FROM_ISR si despertó tarea de mayor prioridad
                               │
                               ▼
              tarea_carga_material[i]   [stack 4 096 B, prioridad 1]
              xQueueReceive(cola_pulsaciones[i], portMAX_DELAY)
              ├─ xQueueSend(cola_receptaculos[i], timeout=0) → FALLA
              │   └─ ESP_LOGW "receptaculo ocupado, pulsacion ignorada"
              └─ xQueueSend(cola_receptaculos[i], timeout=0) → ÉXITO
                  ESP_LOGI "Se acepto la carga de <material>"
                  xQueueSend(cola_materiales_disponibles, portMAX_DELAY)
                               │
                               ▼
              tarea_preparacion          [stack 4 096 B, prioridad 1]
              xQueueReceive(cola_materiales_disponibles, portMAX_DELAY)
              ├─ cola_packs_preparados llena (3/3):
              │   ESP_LOGW + bucle vTaskDelay(100 ms) hasta hueco disponible
              ├─ Inspecciona cola_receptaculos[0..2] → necesita ≥ 2 materiales
              │   └─ < 2 disponibles → continue (descarta aviso, vuelve a esperar)
              ├─ Extrae 2 materiales de cola_receptaculos[]  ← libera receptáculos
              ├─ obtener_codigo_pack(mat_A, mat_B) → código char
              └─ xQueueSend(cola_packs_preparados, portMAX_DELAY)
                 ESP_LOGI "Pack generado con codigo <X>"
                               │
                               ▼
              tarea_procesamiento        [stack 4 096 B, prioridad 1]
              xQueueReceive(cola_packs_preparados, portMAX_DELAY)
              codigo_a_estacion(codigo) → índice [0..2]
              ESP_LOGI "orden <X> enviada a <Estacion>"
              xQueueSend(cola_estacion[idx], portMAX_DELAY)
                               │
              ┌────────────────┼────────────────┐
              ▼                ▼                ▼
        Estación 0       Estación 1       Estación 2
        Estacion Agua    Estacion Arena   Estacion Cemento
        [stack 4 096 B, prioridad 1, cada una independiente]
        xQueueReceive(cola_estacion[i], portMAX_DELAY)
        ESP_LOGI "inicia mezcla con pack <X>"
        vTaskDelay(10 000 ms)
        portENTER_CRITICAL(&mux_mezclas)
        total_mezclas++
        portEXIT_CRITICAL(&mux_mezclas)
        ESP_LOGI "finaliza mezcla. Total: N"

─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─
tarea_panel_estado  [stack 4 096 B, prioridad 1, período 2 s]
Lee: uxQueueMessagesWaiting(cola_receptaculos[0..2])
     uxQueueMessagesWaiting(cola_packs_preparados)
     total_mezclas (sección crítica portMUX_TYPE)
ESP_LOGI "PANEL | Carga Arena:X Agua:X Cemento:X |
          Packs en Preparacion:N/3 | Mezclas completadas:M"
vTaskDelay(2 000 ms)
```

---

## Lógica de combinación de materiales

La función `obtener_codigo_pack(material_a, material_b)` determina el código del pack según los materiales presentes:

| Combinación | Código de pack | `codigo_a_estacion()` retorna | Estación destino |
|---|---|---|---|
| Arena (0) + Cemento (2) | `'W'` | `0` | `Estacion Agua` (`nombre_estacion[0]`) |
| Arena (0) + Agua (1) | `'C'` | `2` | `Estacion Cemento` (`nombre_estacion[2]`) |
| Agua (1) + Cemento (2) | `'S'` | `1` | `Estacion Arena` (`nombre_estacion[1]`) |
| Cualquier otra combinación | `'?'` | `0` (fallback) | `Estacion Agua` (sin log de advertencia) |

> Cada pack contiene exactamente **dos materiales distintos**. El tercer material que llegue mientras dos están en receptáculo esperará en `cola_materiales_disponibles` hasta el próximo ciclo de preparación.

---

## Tareas FreeRTOS

| Nombre en `xTaskCreate` | Función | Instancias | Stack (B) | Prioridad | Bloqueo principal |
|---|---|---|---|---|---|
| `"carga_material"` | `tarea_carga_material` | 3 | 4 096 | 1 | `xQueueReceive(cola_pulsaciones[i], portMAX_DELAY)` |
| `"preparacion"` | `tarea_preparacion` | 1 | 4 096 | 1 | `xQueueReceive(cola_materiales_disponibles, portMAX_DELAY)` |
| `"procesamiento"` | `tarea_procesamiento` | 1 | 4 096 | 1 | `xQueueReceive(cola_packs_preparados, portMAX_DELAY)` |
| `"estacion"` | `tarea_estacion` | 3 | 4 096 | 1 | `xQueueReceive(cola_estacion[i], portMAX_DELAY)` + `vTaskDelay(10 000 ms)` |
| `"panel_estado"` | `tarea_panel_estado` | 1 | 4 096 | 1 | `vTaskDelay(2 000 ms)` |

**Total de tareas de usuario:** 9 · **Total de stack asignado:** 9 × 4 096 = **36 864 bytes**

---

## Sincronización y sección crítica

| Mecanismo | Variable / recurso protegido | Contexto de uso |
|---|---|---|
| `portMUX_TYPE mux_mezclas` + `portENTER_CRITICAL` / `portEXIT_CRITICAL` | `volatile int total_mezclas` | Incremento en `tarea_estacion` (×3) y lectura en `tarea_panel_estado` |
| `QueueHandle_t cola_receptaculos[i]` (cap. 1) | Estado de ocupación del receptáculo | Actúa como mutex binario: cola llena = receptáculo ocupado |
| `xQueueSendFromISR` + `portYIELD_FROM_ISR` | `cola_pulsaciones[i]` | Publicación segura desde contexto de interrupción hacia tarea |
| `ultimo_evento_boton[i]` (`volatile TickType_t`) | Timestamp del último evento GPIO | Filtro antirrebote en ISR (solo lectura/escritura desde ISR) |

---

## Salida de log esperada

Mensajes `ESP_LOGI` / `ESP_LOGW` representativos durante una sesión normal (TAG: `entregable1`):

```
I (XXX) entregable1: Sistema iniciado receptaculos de Arena, Agua y Cemento listos para cargar
I (XXX) entregable1: Botones: Arena=GPIO18, Agua=GPIO19, Cemento=GPIO21
I (XXX) entregable1: Preparacion activa: combina dos materiales y almacena hasta 3 packs
I (XXX) entregable1: Procesamiento activo: 3 estaciones simultaneas e independientes, 10s por mezcla

[Al pulsar GPIO18 — Arena]
I (XXX) entregable1: Se acepto la carga de Arena
I (XXX) entregable1: El receptaculo de Arena queda ocupado hasta su paso a preparación

[Al pulsar GPIO18 nuevamente con receptáculo ocupado]
W (XXX) entregable1: El receptaculo de Arena esta ocupado, pulsacion ignorada

[Al pulsar GPIO21 — Cemento, con Arena ya en receptáculo]
I (XXX) entregable1: Se acepto la carga de Cemento
I (XXX) entregable1: El receptaculo de Cemento queda ocupado hasta su paso a preparación
I (XXX) entregable1: Preparacion recoge Arena y Cemento
I (XXX) entregable1: Pack generado con codigo W
I (XXX) entregable1: Procesamiento: orden W enviada a Estacion Agua
I (XXX) entregable1: Estacion Agua inicia mezcla con pack W

[10 segundos después]
I (XXX) entregable1: Estacion Agua finaliza mezcla. Total mezclas completadas: 1

[Panel de estado — cada 2 s]
I (XXX) entregable1: PANEL | Carga Arena:No Agua:No Cemento:No | Packs en Preparacion:0/3 | Mezclas completadas:1

[Backpressure activo]
W (XXX) entregable1: Preparacion llena (3/3): no recoge mas materiales hasta que Procesamiento libere hueco
```

---

## Troubleshooting

| Problema | Causa probable | Solución |
|---|---|---|
| `idf.py: command not found` | Entorno ESP-IDF no exportado en la sesión actual | Ejecutar `export.sh` / `export.ps1` (paso 2) antes de cualquier comando `idf.py` |
| Error `cmake_minimum_required` | Versión de CMake inferior a 3.16 | Actualizar CMake o usar exclusivamente la cadena de herramientas oficial de ESP-IDF |
| Sin salida en el monitor serie | Puerto incorrecto o baudrate distinto | Verificar el puerto en el Administrador de dispositivos; ESP-IDF usa **115 200 bps** por defecto |
| Pulsación ignorada de forma repetida | Receptáculo ocupado: `cola_receptaculos[i]` está llena | Esperar a que `tarea_preparacion` consuma el material y libere el receptáculo automáticamente |
| Preparación bloqueada indefinidamente | `cola_packs_preparados` con 3/3 y estaciones ocupadas | Esperar ≤ 10 s a que una estación finalice su mezcla; el sistema se desbloquea solo |
| Contador `total_mezclas` no avanza | Estaciones no reciben packs | Revisar en el log que `tarea_procesamiento` emita `"orden X enviada a Estacion Y"`; verificar que se pulsen **dos botones de materiales distintos** |
| Preparación descarta aviso sin generar pack | Solo hay un material en receptáculo | El sistema requiere **exactamente 2 materiales distintos** simultáneos; pulsar un segundo botón diferente |
| IntelliSense no resuelve símbolos de ESP-IDF | Rutas de `c_cpp_properties.json` no coinciden con instalación local | Ajustar `idf.espIdfPathWin` y `idf.toolsPathWin` en `.vscode/settings.json` al entorno local y recargar VS Code |
| Error al registrar ISR (`gpio_install_isr_service`) | Servicio ISR ya instalado por otro componente | Pendiente de confirmar: verificar si algún componente de ESP-IDF inicializado automáticamente ya llama a este servicio |

---

## Estado del proyecto

| Ítem | Estado |
|---|---|
| Pipeline completo (Carga → Preparación → Procesamiento → Mezcla) | ✅ Implementado y verificado en `main/main.c` |
| ISR con filtro antirrebote por software (1 000 ms) | ✅ Implementado (`isr_boton` + `TIEMPO_ANTIRREBOTE`) |
| Receptáculo exclusivo por material (`cola_receptaculos`, cap. 1) | ✅ Implementado |
| Backpressure en `cola_packs_preparados` (sondeo + 100 ms) | ✅ Implementado |
| Panel de estado periódico cada 2 s | ✅ Implementado (`tarea_panel_estado`) |
| Contador global thread-safe (`portMUX_TYPE`) | ✅ Implementado (`total_mezclas` + `mux_mezclas`) |
| Comunicación ISR→tarea vía `xQueueSendFromISR` + `portYIELD_FROM_ISR` | ✅ Implementado |
| Manejo de errores en inicialización (colas, tareas, ISR) | ⚠️ No implementado — valores de retorno de `xQueueCreate` y `xTaskCreate` no se verifican |
| Fallback de código de pack `'?'` | ⚠️ Ruta de código presente (`default: return 0`) sin log de advertencia; comportamiento silencioso |
| Liberación de receptáculo post-mezcla | ⚠️ Pendiente de confirmar — el receptáculo se libera al extraer el material en `tarea_preparacion`; validar si el enunciado exige liberación adicional tras la mezcla |
| `doc/documentacion_funcional_main.md` sincronizada con implementación actual | ⚠️ El documento describe una versión anterior con semáforos binarios; **no refleja la implementación con colas** de la versión actual |
| Pruebas unitarias / de integración | 🔲 No implementadas |
| CI/CD (GitHub Actions u otro) | 🔲 No configurado (`.github/agents/` presente pero sin workflows de build/test) |

---

## Documentación adicional

| Archivo | Descripción |
|---|---|
| [`doc/Entregable_1.pdf`](doc/Entregable_1.pdf) | Enunciado oficial del entregable con requisitos normativos del sistema |
| [`doc/documentacion_funcional_main.md`](doc/documentacion_funcional_main.md) | Análisis funcional con trazabilidad de requisitos ⚠️ *Generado sobre versión anterior del código con semáforos; las brechas descritas no aplican directamente a la implementación actual con colas* |

---

## Tecnologías utilizadas

| Componente | Detalle verificado en el repositorio |
|---|---|
| SoC / Hardware | ESP32 (Xtensa LX6 dual-core) |
| Framework | ESP-IDF v5.5.3 |
| RTOS | FreeRTOS (integrado en ESP-IDF) |
| Lenguaje | C |
| Compilador | xtensa-esp32-elf-gcc · esp-14.2.0_20251107 (fuente: `.vscode/c_cpp_properties.json`) |
| Sistema de build | CMake ≥ 3.16 + `idf.py` |
| Depuración | OpenOCD + GDB — Eclipse CDT GDB Adapter / extensión ESP-IDF (fuente: `.vscode/launch.json`) |
| IDE recomendado | Visual Studio Code + extensión oficial ESP-IDF |
| Servidor de lenguaje | clangd · esp-clang esp-19.1.2_20250312 (fuente: `.vscode/settings.json`) |
