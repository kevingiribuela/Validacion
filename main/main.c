/*************************************************************************
**************************************************************************
*****************************   BIBLIOTECAS   ****************************
**************************************************************************
**************************************************************************/
 /* Bibliotecas FreeRTOS */
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/task.h"

/* Bibliotecas para avisos de ESP32 */
#include "esp_system.h"
#include "esp_log.h"
#include "esp_event.h"

/* Bibliotecas para el manejo de memoria no-volatil */
#include "nvs_flash.h"
#include "nvs.h"

#include <sys/param.h>
/* Bibliotecas para el WebServer */
#include <esp_http_server.h>
#include "WebServer.h"

/* Bibliotecas de WiFi*/
#include "WiFi.h"

/* Bibliotecas para los puertos GPIO */ 
#include "driver/gpio.h"

/* Bibliotecas de protocolo de comunicaciones */ 
#include "lwip/err.h"
#include "lwip/sys.h"
#include "lwip/dns.h"

/* Bibliotecas para el Socket */
#include "lwip/inet.h"
#include "lwip/netdb.h"
#include "lwip/sockets.h"
#include "ping/ping_sock.h"

/* Bibliotecas extras */
#include "stdbool.h"
#include "ctype.h"

/* Bibliotecas para el ADC */
#include "driver/adc.h"
#include "esp_adc_cal.h"

/* Bibliotecas para el manejo de timers e interrupciones */
#include <stddef.h>
#include "esp_intr_alloc.h"
#include "esp_attr.h"
#include "driver/timer.h"


/************************************************************************
*************************************************************************
******************************   DEFINES   ******************************
*************************************************************************
*************************************************************************/
// Definicion de nucleos de trabajo
#define PROTOCOL_CORE 0
#define PROCESSING_CORE 1

// Definicion de ADC
// fs = (sample_rate*NO_samples)^-1
#define DEFAULT_VREF    1100
#define NO_OF_SAMPLES   8      // Multisampling
#define SAMPLE_RATE 100          // uS 

// Definicion de IP de máquina a la cual mandar dato y puerto
#define PORT 50

// Bits de señalización para los eventos WiFi en RTOS
#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT      BIT1

// Parametros que definen el numero de reintentos y tiempo de espera 
#define MAX_RETRY     20    // Numero de reintentos cada 5 segundos
#define TIME_OUT_WIFI 120   // Segundos en espera antes de conectarse con los datos por default


// Definicion de OFF y ON para el LED
#define OFF 0
#define ON 1

// Definicion de largo de cola de mensajes
#define LARGO_COLA 5

// Definicion plataforma que se utilice ----> Opciones: PROTOBOARD | WIMUMO | PLACA
// Canal 5 es para entrar derecho al ADC, Canal 7 es para conectar al electrodo
#define PLACA 1
// Definicion para utilizar el GPIO 33 (el canal 5 del ADC) par ausar de testpoint para calcular el clock de interrupcion
#define TEST 1      

#ifdef PROTOBOARD
    #define PULSADOR GPIO_NUM_36
    #define LED      GPIO_NUM_2
    #define ADC_CHAN ADC_CHANNEL_6
#elif WIMUMO
    #define PULSADOR GPIO_NUM_2
    #define LED      GPIO_NUM_12
    #define ADC_CHAN ADC_CHANNEL_7
#elif PLACA
    #define PULSADOR GPIO_NUM_15
    #define LED      GPIO_NUM_13
    #define ADC_CHAN ADC_CHANNEL_7
#endif

#ifdef TEST
    #define TEST_POINT GPIO_NUM_33
#endif 

/*************************************************************************
**************************************************************************
*************************   VARIABLES GLOBALES   *************************
**************************************************************************
**************************************************************************/

// A continuación, variables globales utilizadas por diferentes bibliotecas para comunicarse 
//con las funciones cuando suceden diferentes eventos
bool wifi_ok = false, acces_point = false, en_socket=false;
bool parametters = false, loop = false, estado=false;

bool guardar_datos=false, config_default=false;
int retry_conn = 0, i=0;
size_t required_size;

nvs_handle_t my_handle;
EventGroupHandle_t s_wifi_event_group;
QueueHandle_t cola;
esp_netif_t *sta_object, *ap_object;

static intr_handle_t s_timer_handle;

// Para el socket
enum estados{espera, configuracion, acumulacion};

// Variables utilizadas para la configuracion del ADC
static esp_adc_cal_characteristics_t *adc_chars;

static const adc_channel_t channel = ADC_CHAN;
static const adc_bits_width_t width = ADC_WIDTH_BIT_12;

static const adc_atten_t atten = ADC_ATTEN_DB_6;
static const adc_unit_t unit = ADC_UNIT_1;

uint32_t adc_reading = 0;
uint32_t *adc_reading_p;
int num_samples = 0;

int toggle=0;

static const char *TAG = "TESINA";

/*************************************************************************
**************************************************************************
******************* PROTOTIPO DE FUNCIONES/MANJEADORES *******************
**************************************************************************
**************************************************************************/

/* void wifi_event_handler
* Se trata del manejador de enventos WiFi, sus parámetros son el evento base, id del evento, y el dato que 
* aporta dicho evento. 
* WIFI_EVENT: Si se trata de eventos WiFi analiza si se ha iniciado en modo STA, si conectó como
*             STA o se desconectó del STA, o si simplemente la conexión falló. 
*               1 - WIFI_EVENT_STA_START: Inicia el protocolo de conexión.
*               2 - WIFI_EVENT_STA_CONNECTED: Indica que se pudo conectar al AP.
*               3 - WIFI_EVENT_STA_DISCONNECTED: Indica que se desconectó del AP, entonces inicia el proceso
*                                               de reconexion a la red hasta un maximo de MAX_RETRY intentos.
*                                               Si no se puede conectar, vuelve a modo AP reseteando el ESP.
*               4 - WIFI_EVENT_AP_START: Se inició el AP.
* IP_EVENT: Al ocurrir un evento con el IP, el único que puede suceder es que se obtuvo un IP para el 
*           dispositivo, de esta forma, se indica con un led verde que estamos conectados a WiFi. */
void wifi_event_handler(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data){
    if(event_base == WIFI_EVENT){
        if (event_id == WIFI_EVENT_STA_START){
            vTaskDelay(100/portTICK_PERIOD_MS);
            printf("CONECTANDO A WIFI...\n");
            esp_err_t wifi = esp_wifi_connect();    // Connecting
            if(wifi!=ESP_OK){
                xEventGroupSetBits(s_wifi_event_group, WIFI_FAIL_BIT);
                printf("NO SE PUDO CONECTAR A WIFI...\n");
            }
            else{
                printf("CONECTADO A WIFI...\n\n");
            }
            
        }
        else if(event_id == WIFI_EVENT_STA_CONNECTED){
            printf("CONEXION AL AP EXITOSA!\n");
        }
        else if(event_id == WIFI_EVENT_STA_DISCONNECTED){
            wifi_ok=false;
            if(retry_conn<MAX_RETRY){
                if(loop==true){
                    esp_wifi_connect(); // Trying to reconnect
                    retry_conn++;
                    printf("Intento de reconexion Nro: %d de %d\n", retry_conn, MAX_RETRY);
                    for(i=0; i<5;i++){
                        vTaskDelay(1000/portTICK_PERIOD_MS);
                    }
                }
            }
            else{
                xEventGroupSetBits(s_wifi_event_group, WIFI_FAIL_BIT); // Flag to reset ESP
                retry_conn=0;
            }
        }
        else if(event_id == WIFI_EVENT_AP_START){
            acces_point=true;
            }
        else if(event_id == WIFI_EVENT_AP_STOP){
            acces_point=false;
        }
    }
    else if(event_base == IP_EVENT){
        if(event_id == IP_EVENT_STA_GOT_IP){
            wifi_ok=true;
            retry_conn=0;
            printf("IP OBTENIDA!\n\n");
            for(int i =0; i<10; i++){
                gpio_set_level(LED, OFF);
                vTaskDelay(50/portTICK_PERIOD_MS);
                gpio_set_level(LED, ON);
                vTaskDelay(50/portTICK_PERIOD_MS);
            }
            xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
        }      
    }
}

void vBlinking(void *pvParameters){
    while(1){
        // Enclavo el LED si me conecto. Los 100mS son para que no salte el watchdog
        if(wifi_ok){
            gpio_set_level(LED, ON);
            vTaskDelay(300/portTICK_PERIOD_MS);
        }

        // Destello cada 600mS cuando estoy en modo AP
        else if(acces_point){
            gpio_set_level(LED, ON);
            vTaskDelay(300/portTICK_PERIOD_MS);
            gpio_set_level(LED, OFF);
            vTaskDelay(300/portTICK_PERIOD_MS);
        }

        // Si ninguna condicion se cumple, genero un retardo para que no salte el watchdog
        else{
            vTaskDelay(100/portTICK_PERIOD_MS);
        }
    }
}

void vPulsador(void *pvParameters){
    while(1){
        if(gpio_get_level(PULSADOR)&&acces_point){  // Habilito conexion default (debe haber una conexion previa en memoria)
            config_default=true;
        }
        else if(gpio_get_level(PULSADOR)&&wifi_ok){ // Habilito/deshabilito el socket
            if(en_socket){
                en_socket=false;
                estado=espera;
            }
            else{
                en_socket=true;
            }
        }
        else{
            vTaskDelay(100/portTICK_PERIOD_MS);
        }
    }
}

static void check_efuse(void){
    //Check if TP is burned into eFuse
    if (esp_adc_cal_check_efuse(ESP_ADC_CAL_VAL_EFUSE_TP) == ESP_OK) {
        printf("eFuse Two Point: Supported\n");
    } else {
        printf("eFuse Two Point: NOT supported\n");
    }
    //Check Vref is burned into eFuse
    if (esp_adc_cal_check_efuse(ESP_ADC_CAL_VAL_EFUSE_VREF) == ESP_OK) {
        printf("eFuse Vref: Supported\n");
    } else {
        printf("eFuse Vref: NOT supported\n");
    }
}

static void print_char_val_type(esp_adc_cal_value_t val_type){
    if (val_type == ESP_ADC_CAL_VAL_EFUSE_TP) {
        printf("Characterized using Two Point Value\n");
    } else if (val_type == ESP_ADC_CAL_VAL_EFUSE_VREF) {
        printf("Characterized using eFuse Vref\n");
    } else {
        printf("Characterized using Default Vref\n");
    }
}

static void ADC(void* arg)
{
    TIMERG0.int_clr_timers.t0 = 1;
    TIMERG0.hw_timer[0].config.alarm_en = 1;

    if(num_samples == NO_OF_SAMPLES-1){
        adc_reading /= NO_OF_SAMPLES;
        adc_reading_p = &adc_reading;
        xQueueSendFromISR(cola, adc_reading_p, portMAX_DELAY);
        num_samples = 0;
        adc_reading = 0;
        // Toggle para medir la frecuencia del reloj
        if(toggle==0){gpio_set_level(TEST_POINT, ON); toggle=1;}
        else if(toggle==1){gpio_set_level(TEST_POINT, OFF); toggle=0;}
    }
    else{
        adc_reading += adc1_get_raw((adc1_channel_t)channel);       
        num_samples++;
    }
}

void init_timer(int timer_period_us)
{
    timer_config_t config = {
            .alarm_en = true,
            .counter_en = false,
            .intr_type = TIMER_INTR_LEVEL,
            .counter_dir = TIMER_COUNT_UP,
            .auto_reload = true,
            .divider = 80   /* 1 us per tick */
    };
    
    timer_init(TIMER_GROUP_0, TIMER_0, &config);
    timer_set_counter_value(TIMER_GROUP_0, TIMER_0, 0);
    timer_set_alarm_value(TIMER_GROUP_0, TIMER_0, timer_period_us);
    timer_enable_intr(TIMER_GROUP_0, TIMER_0);
    timer_isr_register(TIMER_GROUP_0, TIMER_0, &ADC, NULL, 0, &s_timer_handle);

    timer_start(TIMER_GROUP_0, TIMER_0);
}

void vSocket(void *pvParameters){
    enum estados estado;
    estado=espera;

    uint32_t buffer1[LARGO_COLA];

    for(int i=0;i<LARGO_COLA;i++){
        buffer1[i]=0;
    }

    uint32_t *buffer_pointer;
    int cantidad_elementos = 0;

    buffer_pointer = &buffer1[0];
    
    while (1){
        switch (estado){
            case espera:
                    if(en_socket){estado=configuracion;}
                    else{vTaskDelay(10/portTICK_PERIOD_MS);}
                break;
            case configuracion:
                    /**********************************/
                    // Abro la memoria no volatil en modo escritura/lectura
                    nvs_open("wifi",NVS_READWRITE, &my_handle);                 
                    nvs_get_str(my_handle, "IP", NULL, &required_size);
                    char *ip = malloc(required_size);
                    nvs_get_str(my_handle, "IP", ip, &required_size);

                    nvs_close(my_handle);
                    char host_ip[15];
                    strcpy(host_ip, ip);
                    free(ip);
                    /**********************************/

                    int addr_family = 0;
                    int ip_protocol = 0;
                    int sock=0, err=0;
                    struct sockaddr_in dest_addr;
                    dest_addr.sin_addr.s_addr = inet_addr(host_ip);
                    dest_addr.sin_family = AF_INET;
                    dest_addr.sin_port = htons(PORT);
                    addr_family = AF_INET;
                    ip_protocol = IPPROTO_IP;
                    sock = socket(addr_family, SOCK_STREAM, ip_protocol);
                    if (sock < 0){
                        ESP_LOGE(TAG, "No se pudo crear el socket, error: %d", errno);
                        break;
                    }
                    ESP_LOGI(TAG, "Socket creado, conectando a %s:%d", host_ip, PORT);

                    err = connect(sock, (struct sockaddr *)&dest_addr, sizeof(struct sockaddr_in6));
                    if (err != 0){
                        ESP_LOGE(TAG, "El socket no se ha podido conectador, error: %d", errno);
                        break;
                    }
                    ESP_LOGI(TAG, "Conectado exitosamente!");
                    estado=acumulacion;
                break;
            case acumulacion:
                    if(cantidad_elementos<LARGO_COLA){
                        xQueueReceive(cola, buffer_pointer++, portMAX_DELAY);
                        cantidad_elementos++;
                    }
                    else if(cantidad_elementos==LARGO_COLA){
                        cantidad_elementos=0;
                        buffer_pointer = &buffer1[0];
                        //estado=envio;
                        err = send(sock, buffer_pointer, sizeof(buffer1), 0);
                        if (err < 0){
                        ESP_LOGE(TAG, "Error durante el envio, error: %d", errno);
                        }
                    }
                break;
            default:
                break;
        }
    }
}

void app_main(void){
    int time_out=TIME_OUT_WIFI;
    static httpd_handle_t server = NULL;

    s_wifi_event_group = xEventGroupCreate(); // Create event group for wifi events
   
   // Inicializo la memoria no volatil para almacenar datos de red. Si esta llena, la borro y continuo.
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND){
      ESP_ERROR_CHECK(nvs_flash_erase());
      ret = nvs_flash_init();
    }
    printf("\nNVS INICIALIZADA CORRECTAMENTE.\n");

    // Inicializo protocolo TCP/IP
    ESP_ERROR_CHECK(esp_netif_init());
    printf("\nPROTOCOLO TCP/IP INICIALIZADO CORRECTAMENTE.\n");

    // Creo el event loop por defecto (responde a eventos de WiFi)
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    printf("\nEVENTO LOOP CREADO CORRECTAMENTE.\n");

    //Check if Two Point or Vref are burned into eFuse
    check_efuse();

    //Configure ADC
    adc1_config_width(width);
    adc1_config_channel_atten(channel, atten);

    //Characterize ADC
    adc_chars = calloc(1, sizeof(esp_adc_cal_characteristics_t));
    esp_adc_cal_value_t val_type = esp_adc_cal_characterize(unit, atten, width, DEFAULT_VREF, adc_chars);
    print_char_val_type(val_type);

    // Configuro el GPIO del led y un pin cualquiera para testeo de ISR
    gpio_reset_pin(LED);
    gpio_set_direction(LED, GPIO_MODE_OUTPUT);
    gpio_set_level(LED, OFF);

    gpio_reset_pin(TEST_POINT);
    gpio_set_direction(TEST_POINT, GPIO_MODE_OUTPUT);
    gpio_set_level(TEST_POINT, OFF);

    // Configuro el GPIO del pulsador
    gpio_reset_pin(PULSADOR);
    gpio_set_direction(PULSADOR, GPIO_MODE_INPUT);

    // Seteo los manejadores de WiFi y obtencion de IP
    esp_event_handler_instance_t instance_any_id;
    esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL, &instance_any_id);

    esp_event_handler_instance_t instance_got_ip;
    esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_event_handler, NULL, &instance_got_ip);
    
    // Creo la tarea de señalizacion de estados de conexion inalambrica
    xTaskCreate(vBlinking, "Iluminacion", 1024, NULL, 0, NULL);
    xTaskCreate(vPulsador, "Conectar en default", 1024, NULL, 1, NULL);
    xTaskCreatePinnedToCore(vSocket, "Socket", 4096, NULL, configMAX_PRIORITIES-1, NULL, PROCESSING_CORE);
    cola = xQueueCreate(LARGO_COLA, sizeof(uint32_t));
    xQueueReset(cola);

    // Inicio de super lazo.
    while(true){
        // Inicio al ESP32 en modo Acces Point y le adjunto los manejadores de evento.
        ap_object = wifi_init_softap();
        ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &connect_handler, &server));
        ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, WIFI_EVENT_STA_DISCONNECTED, &disconnect_handler, &server));
        
        // Inicializo el WebServer y espero por el ingreso de parámetros por AP, luego, lo detengo y desadjunto los manejadores.
        server = start_webserver();
        while(parametters != true){
            vTaskDelay(1000/portTICK_PERIOD_MS);
            time_out--;
            if((time_out==0)||config_default){
                time_out=TIME_OUT_WIFI;                                     // Si se acabo el tiempo de espera, reinicio el contador
                parametters=true;                                           // Activo flag de parametros ingresados (se utilizan los default)
                config_default=false;

                nvs_open("wifi",NVS_READWRITE, &my_handle);                 // Abro la memoria no volatil en modo escritura/lectura
                
                nvs_get_str(my_handle, "SSID", NULL, &required_size);       // Obtengo el espacio en memoria requerido para almacenar el SSID
                char *wifi_ssid = malloc(required_size);                    // Lo asigno dinamicamente
                nvs_get_str(my_handle, "SSID", wifi_ssid, &required_size);  // Lo almaceno en el espacio generado dinamicamente

                nvs_get_str(my_handle, "PSWD", NULL, &required_size);       // Obtengo el espacio en memoria requerido para almacenar el SSID
                char *wifi_pswd = malloc(required_size);                    // Lo asigno dinamicamente
                nvs_get_str(my_handle, "PSWD", wifi_pswd, &required_size);  // Lo almaceno en el espacio generado dinamicamente

                nvs_get_str(my_handle, "PROTOCOLO", NULL, &required_size);  // Get the required size, and value of the SSID from NVS
                char *protocolo = malloc(required_size);
                nvs_get_str(my_handle, "PROTOCOLO", protocolo, &required_size);

                nvs_get_str(my_handle, "IP", NULL, &required_size);
                char *ip = malloc(required_size);
                nvs_get_str(my_handle, "IP", ip, &required_size);

                // Muestro por consola los valores utilizados por defecto
                printf("Iniciando conexion default...\n");
                printf("SSID: %s\n", wifi_ssid);
                printf("PSWD: %s\n", wifi_pswd);
                printf("PROTOCOLO: %s\n\n", protocolo);
                printf("IP: %s", ip);

                // Cierro el manejador de la memoria no volatil y libero memoria dinamica
                nvs_close(my_handle);
                free(wifi_pswd);
                free(wifi_ssid);
                free(protocolo);
                free(ip);
            }
        }
        stop_webserver(server);
        
        ESP_ERROR_CHECK(esp_event_handler_unregister(IP_EVENT, IP_EVENT_STA_GOT_IP, &connect_handler));
        ESP_ERROR_CHECK(esp_event_handler_unregister(WIFI_EVENT, WIFI_EVENT_STA_DISCONNECTED, &disconnect_handler));
        
        // Inicio al ESP32 en modo Station
        sta_object = wifi_init_sta();
        
        // Si la conexion a WiFi es exitosa, habilito el super loop. Sino, regreso al modo AP
        if(wifi_ok){
            loop=true;
            init_timer(SAMPLE_RATE);
        }
        else{
            loop=false;
            ESP_LOGW(TAG,"No se pudo conectar al AP. Volviendo a modo AP...");
        }

        while(loop){
            // Si algo sale mal con el WiFi, los manejadores externos setean wifi_ok=FALSE.
            if(!wifi_ok){
                EventBits_t bits = xEventGroupWaitBits(s_wifi_event_group,
                                                        WIFI_CONNECTED_BIT | WIFI_FAIL_BIT,
                                                        pdTRUE,
                                                        pdFALSE,
                                                        portMAX_DELAY); // <<---- portMAX_DELAY bloquea el programa hasta que uno de los parametros cambie
                // Analizo el flag de conexion, si esta en alto, continua el modo de operacion. Sino, se vuelve al modo AP
                if(bits & WIFI_CONNECTED_BIT){
                    wifi_ok=true;      
                }
                else if(bits & WIFI_FAIL_BIT){
                    loop=false;
                }
            }
            else{
                vTaskDelay(100/portTICK_PERIOD_MS);
            }
        } 
        
        // Reseteo flags de parametros ingresado, tiempo de espera, detengo el modulo WiFi, destruyo los manejadores de AP y STA
        // y vuelvo al modo AP.
        parametters=false;
        time_out = TIME_OUT_WIFI;
        esp_wifi_stop();
        esp_netif_destroy_default_wifi(sta_object);
        esp_netif_destroy_default_wifi(ap_object);
    }
}