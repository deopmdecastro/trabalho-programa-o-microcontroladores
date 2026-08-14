////////////////////////////////////////////////////////////////////////
/// @copyright ATEC
////////////////////////////////////////////////////////////////////////
///
/// @brief  Ficheiro de configuração do firmware
///         Sistema de Arrefecimento de Sala de Máquinas
///
/// @version 1.0
///
////////////////////////////////////////////////////////////////////////
///
/// @authors  Grupo
/// @date     2026-01-30
///
////////////////////////////////////////////////////////////////////////

#ifndef CONFIG_HPP_INCLUDED_
#define CONFIG_HPP_INCLUDED_

#include <Arduino.h>

// ============================================================================
// MAPEAMENTO DE PINOS - STM32 Nucleo-L476RG
// ============================================================================

// Sensor de temperatura e humidade (DHT11)
#define DHT_PIN       PC1       // Pino de dados do DHT11
#define DHT_TYPE      DHT11     // Tipo de sensor

// LEDs de sinalização de estado
#define LED_GREEN_PIN PC12      // LED Verde  -> atuadores ON/OFF / pisca a 1 Hz
#define LED_RED_PIN   PC10      // LED Vermelho -> erro no sistema

// Botão do utilizador (active LOW com pull-up interno)
#define BTN_USER_PIN  PC13      // Botão USER da placa Nucleo

// Interface SPI para cartão microSD (memória externa)
static constexpr int SCK_PIN    = PA5;  // SPI Clock
static constexpr int MISO_PIN   = PA6;  // SPI MISO
static constexpr int MOSI_PIN   = PA7;  // SPI MOSI
static constexpr int SD_CS_PIN  = PB6;  // Chip Select do módulo SD

// ============================================================================
// SETPOINTS DE TEMPERATURA (em décimos de ºC para evitar float)
//   25.0 ºC -> 250   |   20.0 ºC -> 200
// ============================================================================
static constexpr int16_t TEMP_SETPOINT_MAX = 250;  // 25.0 ºC
static constexpr int16_t TEMP_SETPOINT_MIN = 200;  // 20.0 ºC

// ============================================================================
// TEMPORIZAÇÃO (ms)
// ============================================================================
static constexpr uint32_t TICK_INTERVAL_1HZ  = 1000; // Intervalo de leitura dos sensores
static constexpr uint32_t BLINK_INTERVAL_MS  =  500; // Semi-período do pisca-pisca (LED verde)
static constexpr uint32_t BTN_LONG_PRESS_MS  = 1500; // Limiar clique longo
static constexpr uint32_t BTN_DEBOUNCE_MS    =   50; // Debounce mínimo para clique curto

// ============================================================================
// COMUNICAÇÃO SERIAL
// ============================================================================
static constexpr uint32_t SERIAL_BAUDRATE = 115200;

// ============================================================================
// CONFIGURAÇÕES DE DEBUG
// ============================================================================
static constexpr bool IS_DEBUG_PRINT  = false; // Mensagens de debug extra
static constexpr bool IS_DEBUG_LOG    = false; // Logs de debug no SD
static constexpr bool IS_SERIAL_PRINT = true;  // Mensagens no Monitor Serial
static constexpr bool IS_RTC_ENABLED  = true;  // Usar RTC para timestamps

// ============================================================================
// SISTEMA DE FICHEIROS - Cartão SD
// ============================================================================

// Nomes dos ficheiros (formato 8.3 para compatibilidade FAT16/FAT32)
static const char CONFIG_FILENAME[] = "config.csv";   // Ficheiro de configuração
static const char LOG_FILENAME[]    = "system.log";   // Log de eventos do sistema
static const char CSV_FILENAME[]    = "data.csv";     // Log de leituras dos sensores

// Cabeçalho do CSV de dados (temperatura/humidade)
static const char CSV_HEADER[]      = "timestamp,temp_raw,hum_raw,temp_c,hum_pct,state\r\n";

// Cabeçalho do CSV de configuração
static const char CONFIG_HEADER[]   = "param,value\r\n";

// Tamanho máximo de um ficheiro de log antes de rotação (1 MB)
static constexpr uint32_t MAX_FILE_SIZE = 1048576UL;

// ============================================================================
// ESTRUTURA DE DATA/HORA (usada pelo RTC)
// ============================================================================
struct DateTime_t
{
    uint16_t year;
    uint8_t  month;
    uint8_t  day;
    uint8_t  hours;
    uint8_t  minutes;
    uint8_t  seconds;
};

// ============================================================================
// ESTRUTURA DE CONFIGURAÇÃO DO SISTEMA
//   Lida a partir do ficheiro config.csv no cartão SD no arranque.
//   Caso o ficheiro não exista, são usados os valores padrão definidos aqui.
// ============================================================================
struct SystemConfig
{
    int16_t  temp_max;        // Setpoint máximo em décimos de ºC (padrão: 250 = 25.0 ºC)
    int16_t  temp_min;        // Setpoint mínimo em décimos de ºC (padrão: 200 = 20.0 ºC)
    uint32_t log_interval_ms; // Intervalo entre logs em ms (padrão: 1000)
    bool     rtc_enabled;     // Activar RTC para timestamps

    // Construtor com valores padrão
    SystemConfig()
        : temp_max(TEMP_SETPOINT_MAX),
          temp_min(TEMP_SETPOINT_MIN),
          log_interval_ms(TICK_INTERVAL_1HZ),
          rtc_enabled(IS_RTC_ENABLED)
    {}
};

// Instância global acessível por todos os módulos
extern SystemConfig g_config;

// ============================================================================
// ESTADOS DO SISTEMA
// ============================================================================
enum SystemState : uint8_t
{
    SYS_OFF    = 0, // Desligado  : LED Verde FIXO  / LED Vermelho APAGADO
    SYS_ACTIVE = 1, // Ativo      : LED Verde PISCA / LED Vermelho APAGADO
    SYS_ERROR  = 2  // Erro       : LED Verde APAGADO / LED Vermelho FIXO
};

#endif /* CONFIG_HPP_INCLUDED_ */
