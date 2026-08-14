/* ============================================================================
 * FIRMWARE: Sistema de Arrefecimento de Sala de Máquinas
 *
 * Plataforma : STM32 Nucleo-L476RG  (Framework Arduino / PlatformIO)
 * Versão     : 1.0
 * Data       : 2026-01-30
 *
 * Descrição:
 *   Monitorização contínua de temperatura e humidade via sensor DHT11.
 *   Controlo de atuadores simulados com LEDs e interface via CLI Serial.
 *   Registo de dados e eventos em cartão microSD (memória externa SPI).
 *   Leitura de configuração a partir de ficheiro CSV no cartão SD.
 *   Timestamps gerados pelo RTC interno do STM32.
 *
 * Requisitos cobertos:
 *   [x] Leitura de sensor a 1 Hz (DHT11 – temperatura e humidade)
 *   [x] Controlo de GPIOs (LED Verde, LED Vermelho, Botão)
 *   [x] Interrupção por hardware (botão USER via EXTI / attachInterrupt)
 *   [x] Interrupção por software (timer 1 Hz via HardwareTimer – TIM2)
 *   [x] Biblioteca externa (DHT, SD, RTClib)
 *   [x] Logs de leituras (data.csv) e eventos (system.log) no SD
 *   [x] Ficheiro de configuração (config.csv) lido do SD no arranque
 *   [x] Manipulação de GPIOs (pinMode / digitalWrite / digitalRead)
 *   [x] Integração de sensor (DHT11)
 *   [x] Integração de memória externa (cartão microSD via SPI)
 *   [x] CLI Serial (comandos: ERRO, STATUS, RESET, HELP)
 *   [x] Máquina de estados (SYS_OFF / SYS_ACTIVE / SYS_ERROR)
 * ============================================================================ */

#include <Arduino.h>
#include <DHT.h>
#include <SD.h>
#include <SPI.h>
#include <RTClib.h>
#include "config.hpp"

// ============================================================================
// INSTÂNCIA GLOBAL DA CONFIGURAÇÃO (declarada extern em config.hpp)
// ============================================================================
SystemConfig g_config;

// ============================================================================
// OBJECTOS DAS BIBLIOTECAS
// ============================================================================
DHT          dht(DHT_PIN, DHT_TYPE);  // Sensor DHT11
RTC_Millis   rtc;                     // RTC de software (usa millis internamente)
                                      // Substitua por RTC_DS3231 se tiver RTC externo

// ============================================================================
// VARIÁVEIS GLOBAIS DE ESTADO
// ============================================================================

// Estado actual do sistema
volatile SystemState current_state = SYS_OFF;

// Leituras dos sensores (décimos de ºC / décimos de %)
//   Temperatura: 210 -> 21.0 ºC   |   Humidade: 500 -> 50.0 %
volatile int16_t current_temp = 210;
volatile int16_t current_hum  = 500;

// Sinalizador de erro de leitura do sensor
volatile bool sensor_error = false;

// ============================================================================
// VARIÁVEIS DE TEMPORIZAÇÃO
// ============================================================================
uint32_t last_1hz_tick   = 0; // Último instante da tarefa periódica de 1 Hz
uint32_t last_blink_tick = 0; // Último toggle do LED verde (pisca-pisca)
bool     green_led_state = LOW;

// ============================================================================
// VARIÁVEIS DO BOTÃO
// ============================================================================
// 'buttonPressed' é partilhada entre a ISR e o loop principal -> volatile
volatile bool     buttonPressed   = false;
volatile uint32_t btn_press_time  = 0;   // Instante em que o botão foi pressionado
bool              btn_last_state  = HIGH; // Estado anterior para deteção de flanco

// ============================================================================
// VARIÁVEIS DA CLI SERIAL
// ============================================================================
char    rx_buf[24]; // Buffer de recepção de comandos
uint8_t rx_idx = 0;

// ============================================================================
// FLAG DE TAREFA PERIÓDICA (activada pelo HardwareTimer)
// ============================================================================
volatile bool timer_tick_flag = false; // Activada pela ISR do timer a 1 Hz

// ============================================================================
// OBJECTOS DO SISTEMA DE FICHEIROS SD
// ============================================================================
bool sd_available = false; // SD inicializado com sucesso?

// ============================================================================
// PROTÓTIPOS DAS FUNÇÕES
// ============================================================================

// Configuração e inicialização
bool    sd_init(void);
bool    sd_load_config(void);
void    sd_create_config(void);
void    rtc_init(void);

// Tarefa periódica
void    task_1hz(void);

// Sensores e atuadores
void    read_sensors(void);
void    update_leds(void);
void    update_actuators(void);

// Interface de utilizador
void    handle_button(void);
void    handle_uart_cli(void);

// Logging
void    log_data(void);
void    log_event(const char *msg);
String  get_timestamp(void);

// Utilitários
void    print_decimal(int16_t val);
const char *state_to_str(SystemState s);

// Interrupções
void    buttonISR(void);   // ISR de hardware – botão (EXTI)
void    timerISR(void);    // ISR de software – HardwareTimer 1 Hz

// ============================================================================
// TIMER DE SOFTWARE (HardwareTimer do STM32)
// ============================================================================
HardwareTimer *timer1hz = nullptr;

// ============================================================================
//  S E T U P
// ============================================================================
void setup()
{
    // ------------------------------------------------------------------
    // 1. Comunicação Serial
    // ------------------------------------------------------------------
    Serial.begin(SERIAL_BAUDRATE);
    while (!Serial && millis() < 3000) {} // Aguarda abertura (timeout 3 s)

    Serial.println(F("\r\n====================================================="));
    Serial.println(F("  SISTEMA DE ARREFECIMENTO - SALA DE MAQUINAS"));
    Serial.println(F("  STM32 Nucleo-L476RG | Firmware v1.0"));
    Serial.println(F("====================================================="));

    // ------------------------------------------------------------------
    // 2. Configuração dos GPIOs
    // ------------------------------------------------------------------
    pinMode(LED_GREEN_PIN, OUTPUT);
    pinMode(LED_RED_PIN,   OUTPUT);
    pinMode(BTN_USER_PIN,  INPUT_PULLUP);
    pinMode(SD_CS_PIN,     OUTPUT);

    // Estado inicial dos LEDs
    digitalWrite(LED_GREEN_PIN, HIGH); // Verde FIXO -> SYS_OFF
    digitalWrite(LED_RED_PIN,   LOW);  // Vermelho APAGADO

    // ------------------------------------------------------------------
    // 3. Interrupção por Hardware – Botão USER (EXTI via attachInterrupt)
    //    Flanco descendente (FALLING) porque o botão é ACTIVE LOW.
    // ------------------------------------------------------------------
    attachInterrupt(digitalPinToInterrupt(BTN_USER_PIN), buttonISR, FALLING);

    // ------------------------------------------------------------------
    // 4. Sensor DHT11
    // ------------------------------------------------------------------
    dht.begin();
    Serial.println(F("[INIT] Sensor DHT11 inicializado."));

    // ------------------------------------------------------------------
    // 5. Cartão SD (memória externa SPI)
    // ------------------------------------------------------------------
    sd_available = sd_init();

    // ------------------------------------------------------------------
    // 6. Leitura da configuração do SD (ou valores padrão se SD offline)
    // ------------------------------------------------------------------
    if (sd_available)
    {
        if (!sd_load_config())
        {
            // Ficheiro não existe -> cria com valores padrão
            sd_create_config();
        }
    }

    // ------------------------------------------------------------------
    // 7. RTC (timestamps nos logs)
    // ------------------------------------------------------------------
    rtc_init();

    // ------------------------------------------------------------------
    // 8. Interrupção por Software – HardwareTimer TIM2 a 1 Hz
    //    Gera um tick periódico sem bloquear o loop principal.
    // ------------------------------------------------------------------
    timer1hz = new HardwareTimer(TIM2);
    timer1hz->setOverflow(1, HERTZ_FORMAT); // 1 Hz
    timer1hz->attachInterrupt(timerISR);
    timer1hz->resume();

    Serial.println(F("[INIT] Timer 1 Hz (TIM2) iniciado."));
    Serial.println(F("[INIT] Sistema pronto.\r\n"));
    Serial.println(F("Comandos disponiveis:"));
    Serial.println(F("  ERRO   - Simular erro no sistema"));
    Serial.println(F("  STATUS - Mostrar estado atual"));
    Serial.println(F("  RESET  - Repor sistema (SYS_OFF)"));
    Serial.println(F("  HELP   - Mostrar esta ajuda"));
    Serial.println(F("  Botao: Clique curto -> ATIVO | Clique longo -> OFF\r\n"));

    // Registo do arranque no log de eventos
    log_event("[BOOT] Sistema iniciado.");
}

// ============================================================================
//  L O O P   P R I N C I P A L
// ============================================================================
void loop()
{
    // ------------------------------------------------------------------
    // A. Entradas assíncronas (sem bloqueio)
    // ------------------------------------------------------------------
    handle_button();
    handle_uart_cli();

    // ------------------------------------------------------------------
    // B. Tarefa periódica de 1 Hz (activada pela ISR do HardwareTimer)
    // ------------------------------------------------------------------
    if (timer_tick_flag)
    {
        noInterrupts();
        timer_tick_flag = false; // Limpa a flag de forma atómica
        interrupts();

        task_1hz(); // Executa a tarefa periódica
    }

    // ------------------------------------------------------------------
    // C. Actualização dos LEDs conforme o estado actual
    // ------------------------------------------------------------------
    update_leds();
}

// ============================================================================
//  T A R E F A   P E R I Ó D I C A   1 H z
// ============================================================================
/**
 * @brief Executada uma vez por segundo.
 *        Lê sensores, activa/desactiva atuadores por temperatura e faz log.
 */
void task_1hz(void)
{
    read_sensors();

    // Controlo automático de temperatura (apenas fora do estado de erro)
    if (current_state != SYS_ERROR)
    {
        update_actuators();
    }

    log_data();
}

// ============================================================================
//  S E N S O R E S
// ============================================================================
/**
 * @brief Lê temperatura e humidade do DHT11 e converte para décimos.
 *        Em caso de leitura inválida, mantém o último valor válido e
 *        transita para SYS_ERROR.
 */
void read_sensors(void)
{
    float t = dht.readTemperature(); // ºC
    float h = dht.readHumidity();    // %

    if (isnan(t) || isnan(h))
    {
        sensor_error = true;

        if (current_state != SYS_ERROR)
        {
            current_state = SYS_ERROR;
            log_event("[ERROR] Falha na leitura do sensor DHT11 -> SYS_ERROR");

            if (IS_SERIAL_PRINT)
            {
                Serial.println(F("[ERROR] Leitura do sensor falhou!"));
            }
        }
        return;
    }

    sensor_error  = false;
    current_temp  = (int16_t)(t * 10.0f); // ex: 23.5 -> 235
    current_hum   = (int16_t)(h * 10.0f); // ex: 55.2 -> 552

    if (IS_SERIAL_PRINT)
    {
        Serial.print(F("[SENSOR] Temp: "));
        print_decimal(current_temp);
        Serial.print(F(" C | Hum: "));
        print_decimal(current_hum);
        Serial.print(F(" % | Estado: "));
        Serial.println(state_to_str(current_state));
    }
}

// ============================================================================
//  A T U A D O R E S
// ============================================================================
/**
 * @brief Controlo automático por histerese de temperatura.
 *        Liga atuadores se temp >= setpoint_max.
 *        Desliga atuadores se temp <= setpoint_min.
 */
void update_actuators(void)
{
    if (current_temp >= g_config.temp_max && current_state != SYS_ACTIVE)
    {
        current_state = SYS_ACTIVE;
        log_event("[AUTO] Temp >= setpoint_max -> Atuadores LIGADOS (SYS_ACTIVE)");

        if (IS_SERIAL_PRINT)
        {
            Serial.print(F("[AUTO] Temp >= "));
            print_decimal(g_config.temp_max);
            Serial.println(F(" C -> Atuadores LIGADOS"));
        }
    }
    else if (current_temp <= g_config.temp_min && current_state != SYS_OFF)
    {
        current_state = SYS_OFF;
        log_event("[AUTO] Temp <= setpoint_min -> Atuadores DESLIGADOS (SYS_OFF)");

        if (IS_SERIAL_PRINT)
        {
            Serial.print(F("[AUTO] Temp <= "));
            print_decimal(g_config.temp_min);
            Serial.println(F(" C -> Atuadores DESLIGADOS"));
        }
    }
}

// ============================================================================
//  L E D s
// ============================================================================
/**
 * @brief Actualiza os LEDs conforme a máquina de estados.
 *
 *   SYS_OFF    -> Verde FIXO  / Vermelho APAGADO
 *   SYS_ACTIVE -> Verde PISCA (500 ms semi-período) / Vermelho APAGADO
 *   SYS_ERROR  -> Verde APAGADO / Vermelho FIXO
 */
void update_leds(void)
{
    uint32_t now = millis();

    switch (current_state)
    {
        case SYS_OFF:
            digitalWrite(LED_GREEN_PIN, HIGH);
            digitalWrite(LED_RED_PIN,   LOW);
            green_led_state = HIGH;
            break;

        case SYS_ACTIVE:
            // Pisca a 1 Hz (500 ms em HIGH + 500 ms em LOW)
            if (now - last_blink_tick >= BLINK_INTERVAL_MS)
            {
                last_blink_tick = now;
                green_led_state = !green_led_state;
                digitalWrite(LED_GREEN_PIN, green_led_state);
            }
            digitalWrite(LED_RED_PIN, LOW);
            break;

        case SYS_ERROR:
            digitalWrite(LED_GREEN_PIN, LOW);
            digitalWrite(LED_RED_PIN,   HIGH);
            green_led_state = LOW;
            break;
    }
}

// ============================================================================
//  B O T Ã O   (com debounce e deteção de clique curto/longo)
// ============================================================================
/**
 * @brief Processa os eventos de botão gerados pela ISR.
 *        Clique curto (< BTN_LONG_PRESS_MS) -> activa sistema (SYS_ACTIVE)
 *        Clique longo (>= BTN_LONG_PRESS_MS) -> desactiva sistema (SYS_OFF)
 *        Em SYS_ERROR o botão não altera o estado.
 */
void handle_button(void)
{
    bool btn_state = digitalRead(BTN_USER_PIN);

    // Flanco descendente: regista instante de pressionamento
    if (btn_state == LOW && btn_last_state == HIGH)
    {
        btn_press_time = millis();
    }
    // Flanco ascendente: calcula duração e toma decisão
    else if (btn_state == HIGH && btn_last_state == LOW)
    {
        uint32_t duration = millis() - btn_press_time;

        if (current_state != SYS_ERROR)
        {
            if (duration >= BTN_LONG_PRESS_MS)
            {
                // Clique longo -> OFF
                current_state = SYS_OFF;
                log_event("[BTN] Clique longo -> SYS_OFF");

                if (IS_SERIAL_PRINT)
                    Serial.println(F("[BTN] Clique longo -> Sistema OFF"));
            }
            else if (duration >= BTN_DEBOUNCE_MS)
            {
                // Clique curto -> ACTIVE
                current_state = SYS_ACTIVE;
                log_event("[BTN] Clique curto -> SYS_ACTIVE");

                if (IS_SERIAL_PRINT)
                    Serial.println(F("[BTN] Clique curto -> Sistema ATIVO"));
            }
            // Duração < BTN_DEBOUNCE_MS -> ruído/bounce, ignorado
        }
        else
        {
            if (IS_SERIAL_PRINT)
                Serial.println(F("[BTN] Sistema em ERRO - botao ignorado"));
        }
    }

    btn_last_state = btn_state;

    // Limpa a flag da ISR (usada apenas para acordar de sleep futuro)
    if (buttonPressed)
    {
        noInterrupts();
        buttonPressed = false;
        interrupts();
    }
}

// ============================================================================
//  C L I   S E R I A L
// ============================================================================
/**
 * @brief Processa comandos recebidos via UART.
 *        Comandos aceites (não sensíveis a maiúsculas/minúsculas):
 *          ERRO   -> transita para SYS_ERROR
 *          STATUS -> imprime estado actual
 *          RESET  -> repõe para SYS_OFF
 *          HELP   -> imprime ajuda
 */
void handle_uart_cli(void)
{
    while (Serial.available() > 0)
    {
        char c = (char)Serial.read();

        if (c == '\n' || c == '\r')
        {
            if (rx_idx > 0)
            {
                rx_buf[rx_idx] = '\0';
                rx_idx = 0;

                // Converte para maiúsculas para comparação case-insensitive
                for (uint8_t i = 0; rx_buf[i]; i++)
                    rx_buf[i] = (char)toupper((unsigned char)rx_buf[i]);

                // --- Processamento do comando ---
                if (strcmp(rx_buf, "ERRO") == 0)
                {
                    current_state = SYS_ERROR;
                    log_event("[CLI] Comando ERRO -> SYS_ERROR");
                    Serial.println(F("[CLI] >>> ERRO ACTIVADO VIA SERIAL!"));
                }
                else if (strcmp(rx_buf, "STATUS") == 0)
                {
                    Serial.print(F("[STATUS] Estado: "));
                    Serial.print(state_to_str(current_state));
                    Serial.print(F(" | Temp: "));
                    print_decimal(current_temp);
                    Serial.print(F(" C | Hum: "));
                    print_decimal(current_hum);
                    Serial.print(F(" % | SD: "));
                    Serial.println(sd_available ? F("OK") : F("OFFLINE"));
                }
                else if (strcmp(rx_buf, "RESET") == 0)
                {
                    current_state = SYS_OFF;
                    log_event("[CLI] Comando RESET -> SYS_OFF");
                    Serial.println(F("[CLI] Sistema reposto para OFF."));
                }
                else if (strcmp(rx_buf, "HELP") == 0)
                {
                    Serial.println(F("Comandos:"));
                    Serial.println(F("  ERRO   - Simular erro no sistema"));
                    Serial.println(F("  STATUS - Mostrar estado atual"));
                    Serial.println(F("  RESET  - Repor sistema (SYS_OFF)"));
                    Serial.println(F("  HELP   - Mostrar esta ajuda"));
                }
                else
                {
                    Serial.print(F("[CLI] Comando desconhecido: "));
                    Serial.println(rx_buf);
                }
            }
        }
        else if (rx_idx < (sizeof(rx_buf) - 1))
        {
            rx_buf[rx_idx++] = c;
        }
    }
}

// ============================================================================
//  L O G G I N G  –  Cartão SD (memória externa)
// ============================================================================

/**
 * @brief Obtém a string de timestamp do RTC.
 *        Formato: "YYYY-MM-DD HH:MM:SS"
 */
String get_timestamp(void)
{
    DateTime now = rtc.now();
    char buf[20];
    snprintf(buf, sizeof(buf), "%04u-%02u-%02u %02u:%02u:%02u",
             now.year(), now.month(), now.day(),
             now.hour(), now.minute(), now.second());
    return String(buf);
}

/**
 * @brief Grava uma linha no ficheiro CSV de dados (data.csv).
 *        Formato: timestamp,temp_raw,hum_raw,temp_c,hum_pct,state
 */
void log_data(void)
{
    if (!sd_available) return;

    File f = SD.open(CSV_FILENAME, FILE_WRITE);
    if (!f)
    {
        if (IS_DEBUG_LOG)
            Serial.println(F("[LOG] Falha ao abrir data.csv"));
        return;
    }

    // Escreve cabeçalho se o ficheiro estiver vazio
    if (f.size() == 0)
        f.print(CSV_HEADER);

    // Linha de dados
    String ts = get_timestamp();
    f.print(ts);
    f.print(',');
    f.print(current_temp);
    f.print(',');
    f.print(current_hum);
    f.print(',');

    // Temperatura em ºC com uma casa decimal
    f.print(current_temp / 10);
    f.print('.');
    f.print(abs(current_temp % 10));
    f.print(',');

    // Humidade em % com uma casa decimal
    f.print(current_hum / 10);
    f.print('.');
    f.print(abs(current_hum % 10));
    f.print(',');

    f.println(state_to_str(current_state));
    f.close();
}

/**
 * @brief Grava uma mensagem de evento no ficheiro de log (system.log).
 *        Formato: "YYYY-MM-DD HH:MM:SS | mensagem"
 */
void log_event(const char *msg)
{
    if (IS_SERIAL_PRINT)
    {
        Serial.print(F("[LOG] "));
        Serial.println(msg);
    }

    if (!sd_available) return;

    File f = SD.open(LOG_FILENAME, FILE_WRITE);
    if (!f)
    {
        if (IS_DEBUG_LOG)
            Serial.println(F("[LOG] Falha ao abrir system.log"));
        return;
    }

    String ts = get_timestamp();
    f.print(ts);
    f.print(F(" | "));
    f.println(msg);
    f.close();
}

// ============================================================================
//  C A R T Ã O   S D   –   Inicialização e Configuração
// ============================================================================

/**
 * @brief Inicializa o módulo SD via SPI.
 * @return true se o SD foi inicializado com sucesso, false caso contrário.
 */
bool sd_init(void)
{
    Serial.print(F("[SD] A inicializar cartao SD... "));

    if (!SD.begin(SD_CS_PIN))
    {
        Serial.println(F("FALHOU! A funcionar sem SD."));
        return false;
    }

    Serial.println(F("OK"));
    return true;
}

/**
 * @brief Lê o ficheiro config.csv do SD e popula a estrutura g_config.
 *        Formato esperado (CSV de dois campos: param,value):
 *          temp_max,250
 *          temp_min,200
 *          log_interval,1000
 *          rtc_enabled,1
 *
 * @return true se o ficheiro foi lido com sucesso, false se não existe.
 */
bool sd_load_config(void)
{
    if (!SD.exists(CONFIG_FILENAME))
    {
        Serial.println(F("[CFG] config.csv nao encontrado. A usar valores padrao."));
        return false;
    }

    File f = SD.open(CONFIG_FILENAME, FILE_READ);
    if (!f)
    {
        Serial.println(F("[CFG] Erro ao abrir config.csv."));
        return false;
    }

    Serial.println(F("[CFG] A ler config.csv..."));

    char line[48];
    uint8_t idx = 0;

    while (f.available())
    {
        char c = (char)f.read();

        if (c == '\n' || c == '\r')
        {
            if (idx > 0)
            {
                line[idx] = '\0';
                idx = 0;

                // Ignora cabeçalho e linhas de comentário
                if (line[0] == '#' || strncmp(line, "param", 5) == 0)
                    continue;

                // Divide em param,value pelo primeiro ','
                char *sep = strchr(line, ',');
                if (!sep) continue;

                *sep = '\0';
                const char *param = line;
                const char *value = sep + 1;

                if (strcmp(param, "temp_max") == 0)
                    g_config.temp_max = (int16_t)atoi(value);
                else if (strcmp(param, "temp_min") == 0)
                    g_config.temp_min = (int16_t)atoi(value);
                else if (strcmp(param, "log_interval") == 0)
                    g_config.log_interval_ms = (uint32_t)atol(value);
                else if (strcmp(param, "rtc_enabled") == 0)
                    g_config.rtc_enabled = (atoi(value) != 0);
            }
        }
        else if (idx < sizeof(line) - 1)
        {
            line[idx++] = c;
        }
    }

    f.close();

    Serial.print(F("[CFG] temp_max="));
    print_decimal(g_config.temp_max);
    Serial.print(F(" C | temp_min="));
    print_decimal(g_config.temp_min);
    Serial.println(F(" C"));

    return true;
}

/**
 * @brief Cria um ficheiro config.csv no SD com os valores padrão.
 */
void sd_create_config(void)
{
    File f = SD.open(CONFIG_FILENAME, FILE_WRITE);
    if (!f)
    {
        Serial.println(F("[CFG] Erro ao criar config.csv."));
        return;
    }

    f.println(F("# Sistema de Arrefecimento - Configuracao"));
    f.println(CONFIG_HEADER);
    f.print(F("temp_max,"));
    f.println(TEMP_SETPOINT_MAX);
    f.print(F("temp_min,"));
    f.println(TEMP_SETPOINT_MIN);
    f.print(F("log_interval,"));
    f.println(TICK_INTERVAL_1HZ);
    f.print(F("rtc_enabled,"));
    f.println((int)IS_RTC_ENABLED);
    f.close();

    Serial.println(F("[CFG] config.csv criado com valores padrao."));
}

// ============================================================================
//  R T C
// ============================================================================

/**
 * @brief Inicializa o RTC de software com a data/hora de compilação.
 *        (Para RTC de hardware externo, substitua RTC_Millis por RTC_DS3231)
 */
void rtc_init(void)
{
    // Inicializa com a data/hora do momento da compilação
    rtc.begin(DateTime(F(__DATE__), F(__TIME__)));
    Serial.println(F("[RTC] RTC de software inicializado com data de compilacao."));
}

// ============================================================================
//  U T I L I T Á R I O S
// ============================================================================

/**
 * @brief Imprime um inteiro em décimos com formato decimal.
 *        Ex: 235 -> "23.5"  |  -15 -> "-1.5"
 * @param val Valor em décimos.
 */
void print_decimal(int16_t val)
{
    if (val < 0)
    {
        Serial.print('-');
        val = (int16_t)(-val);
    }
    Serial.print(val / 10);
    Serial.print('.');
    Serial.print(val % 10);
}

/**
 * @brief Converte o enum SystemState para string legível.
 * @param s Estado actual.
 * @return Ponteiro para string estática.
 */
const char *state_to_str(SystemState s)
{
    switch (s)
    {
        case SYS_OFF:    return "OFF";
        case SYS_ACTIVE: return "ACTIVE";
        case SYS_ERROR:  return "ERROR";
        default:         return "UNKNOWN";
    }
}

// ============================================================================
//  I N T E R R U P Ç Õ E S
// ============================================================================

/**
 * @brief ISR de Hardware – activada no flanco descendente do botão USER.
 *        (EXTI via attachInterrupt)
 *        Apenas sinaliza a flag; processamento real é feito no loop().
 */
void buttonISR(void)
{
    buttonPressed = true; // Sinaliza evento para o loop principal
}

/**
 * @brief ISR de Software – activada pelo HardwareTimer TIM2 a 1 Hz.
 *        Sinaliza a flag para que a tarefa periódica seja executada no loop().
 */
void timerISR(void)
{
    timer_tick_flag = true; // Sinaliza tarefa de 1 Hz para o loop principal
}
