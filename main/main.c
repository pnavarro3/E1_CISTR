#include <stdio.h>

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "esp_log.h"

static const char *TAG = "entregable1";
static const TickType_t TIEMPO_ANTIRREBOTE = pdMS_TO_TICKS(1000);

static const int NUM_MATERIALES = 3;
static const int CAPACIDAD_RECEPTACULO = 1;
static const int CAPACIDAD_PACKS = 3;
static const char *materiales[] = { "Arena", "Agua", "Cemento" };
static const gpio_num_t pines_boton[] = { GPIO_NUM_18, GPIO_NUM_19, GPIO_NUM_21 };
static QueueHandle_t cola_pulsaciones[3];
static QueueHandle_t cola_receptaculos[3];
static QueueHandle_t cola_materiales_disponibles;
static QueueHandle_t cola_packs_preparados;
static int id_material[3] = { 0, 1, 2 };
static int id_estacion[3] = { 0, 1, 2 };
static volatile TickType_t ultimo_evento_boton[3] = { 0 };

static QueueHandle_t cola_estacion[3];
static SemaphoreHandle_t mutex_procesamiento;
static volatile int total_mezclas = 0;

static const char *nombre_estacion[] = { "Estacion Agua", "Estacion Arena", "Estacion Cemento" };

static int codigo_a_estacion(char codigo)
{
	switch (codigo) {
		case 'W': return 0;
		case 'S': return 1;
		case 'C': return 2;
		default:  return 0;
	}
}

static char obtener_codigo_pack(int material_a, int material_b)
{
	int tiene_arena = (material_a == 0) || (material_b == 0);
	int tiene_agua = (material_a == 1) || (material_b == 1);
	int tiene_cemento = (material_a == 2) || (material_b == 2);

	if (tiene_arena && tiene_cemento) {
		return 'W';
	}

	if (tiene_arena && tiene_agua) {
		return 'C';
	}

	if (tiene_agua && tiene_cemento) {
		return 'S';
	}

	return '?';
}

static void isr_boton(void *arg)
{
	int i = *(int *)arg;
	BaseType_t tarea_prioritaria_despertada = pdFALSE;
	TickType_t tick_actual = xTaskGetTickCountFromISR();
	uint8_t evento = 1;

	if (ultimo_evento_boton[i] != 0 && (tick_actual - ultimo_evento_boton[i]) < TIEMPO_ANTIRREBOTE) {
		return;
	}

	ultimo_evento_boton[i] = tick_actual;

	xQueueSendFromISR(cola_pulsaciones[i], &evento, &tarea_prioritaria_despertada);
	if (tarea_prioritaria_despertada) {
		portYIELD_FROM_ISR();
	}
}

static void configurar_botones(void)
{
	gpio_config_t cfg = {
		.pin_bit_mask = (1ULL << pines_boton[0]) | (1ULL << pines_boton[1]) | (1ULL << pines_boton[2]),
		.mode = GPIO_MODE_INPUT,
		.pull_up_en = GPIO_PULLUP_ENABLE,
		.pull_down_en = GPIO_PULLDOWN_DISABLE,
		.intr_type = GPIO_INTR_NEGEDGE,
	};

	gpio_config(&cfg);
	gpio_install_isr_service(0);

	for (int i = 0; i < NUM_MATERIALES; i++) {
		gpio_isr_handler_add(pines_boton[i], isr_boton, &id_material[i]);
	}
}

static void tarea_carga_material(void *pvParameters)
{
	int i = *(int *)pvParameters;
	uint8_t evento;
	int material = i;

	while (true) {
		xQueueReceive(cola_pulsaciones[i], &evento, portMAX_DELAY);

		if (xQueueSend(cola_receptaculos[i], &material, 0) != pdTRUE) {
			ESP_LOGW(TAG, "El receptaculo de %s esta ocupado, pulsacion ignorada", materiales[i]);
			continue;
		}

		ESP_LOGI(TAG, "Se acepto la carga de %s", materiales[i]);
		ESP_LOGI(TAG, "El receptaculo de %s queda ocupado hasta su paso a preparación", materiales[i]);
		xQueueSend(cola_materiales_disponibles, &material, portMAX_DELAY);
	}
}

static void tarea_preparacion(void *pvParameters)
{
	(void)pvParameters;
	int material_aviso;

	while (true) {
		int materiales_en_carga[2];
		int cantidad_disponible = 0;

		xQueueReceive(cola_materiales_disponibles, &material_aviso, portMAX_DELAY);

		if (uxQueueSpacesAvailable(cola_packs_preparados) == 0) {
			ESP_LOGW(TAG, "Preparacion llena: hay 3 packs en espera y no se recogen mas materiales");
			continue;
		}

		for (int i = 0; i < NUM_MATERIALES && cantidad_disponible < 2; i++) {
			if (uxQueueMessagesWaiting(cola_receptaculos[i]) > 0) {
				materiales_en_carga[cantidad_disponible] = i;
				cantidad_disponible++;
			}
		}

		if (cantidad_disponible < 2) {
			continue;
		}

		for (int i = 0; i < 2; i++) {
			xQueueReceive(cola_receptaculos[materiales_en_carga[i]], &materiales_en_carga[i], 0);
		}

		char codigo_pack = obtener_codigo_pack(materiales_en_carga[0], materiales_en_carga[1]);
		xQueueSend(cola_packs_preparados, &codigo_pack, portMAX_DELAY);

		ESP_LOGI(TAG, "Preparacion recoge %s y %s", materiales[materiales_en_carga[0]], materiales[materiales_en_carga[1]]);
		ESP_LOGI(TAG, "Pack generado con codigo %c", codigo_pack);
	}
}

static void tarea_procesamiento(void *pvParameters)
{
	(void)pvParameters;
	char codigo_pack;

	while (true) {
		xQueueReceive(cola_packs_preparados, &codigo_pack, portMAX_DELAY);

		int estacion = codigo_a_estacion(codigo_pack);
		ESP_LOGI(TAG, "Procesamiento: orden %c enviada a %s", codigo_pack, nombre_estacion[estacion]);
		xQueueSend(cola_estacion[estacion], &codigo_pack, portMAX_DELAY);
	}
}

static void tarea_estacion(void *pvParameters)
{
	int estacion = *(int *)pvParameters;
	char codigo_pack;

	while (true) {
		xQueueReceive(cola_estacion[estacion], &codigo_pack, portMAX_DELAY);

		xSemaphoreTake(mutex_procesamiento, portMAX_DELAY);

		ESP_LOGI(TAG, "%s inicia mezcla con pack %c", nombre_estacion[estacion], codigo_pack);
		vTaskDelay(pdMS_TO_TICKS(10000));
		total_mezclas++;
		ESP_LOGI(TAG, "%s finaliza mezcla. Total mezclas completadas: %d", nombre_estacion[estacion], total_mezclas);

		xSemaphoreGive(mutex_procesamiento);
	}
}

void app_main(void)
{
	for (int i = 0; i < NUM_MATERIALES; i++) {
		cola_pulsaciones[i] = xQueueCreate(1, sizeof(uint8_t));
		cola_receptaculos[i] = xQueueCreate(CAPACIDAD_RECEPTACULO, sizeof(int));
		xTaskCreate(tarea_carga_material, "carga_material", 4096, &id_material[i], 1, NULL);
	}

	cola_materiales_disponibles = xQueueCreate(NUM_MATERIALES, sizeof(int));
	cola_packs_preparados = xQueueCreate(CAPACIDAD_PACKS, sizeof(char));

	for (int i = 0; i < NUM_MATERIALES; i++) {
		cola_estacion[i] = xQueueCreate(CAPACIDAD_PACKS, sizeof(char));
	}

	mutex_procesamiento = xSemaphoreCreateMutex();

	xTaskCreate(tarea_preparacion, "preparacion", 4096, NULL, 1, NULL);
	xTaskCreate(tarea_procesamiento, "procesamiento", 4096, NULL, 1, NULL);

	for (int i = 0; i < NUM_MATERIALES; i++) {
		xTaskCreate(tarea_estacion, "estacion", 4096, &id_estacion[i], 1, NULL);
	}

	configurar_botones();

	ESP_LOGI(TAG, "Sistema iniciado receptaculos de Arena, Agua y Cemento listos para cargar");
	ESP_LOGI(TAG, "Botones: Arena=GPIO18, Agua=GPIO19, Cemento=GPIO21");
	ESP_LOGI(TAG, "Preparacion activa: combina dos materiales y almacena hasta 3 packs");
	ESP_LOGI(TAG, "Procesamiento activo: 3 estaciones, exclusion mutua basica, 10s por mezcla");

	while (true) {
		vTaskDelay(pdMS_TO_TICKS(1000));
	}
}