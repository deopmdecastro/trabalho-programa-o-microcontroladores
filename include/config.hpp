////////////////////////////////////////////////////////////////////////
/// @copyright ATEC
////////////////////////////////////////////////////////////////////////
///
/// @brief Configuration header file
///
/// @version 0.1
///
////////////////////////////////////////////////////////////////////////
///
/// @authors
/// @date
///
////////////////////////////////////////////////////////////////////////

#ifndef CONFIG_HPP_INCLUDED_
#define CONFIG_HPP_INCLUDED_

#include <string> // Necessário para std::string

// Pinos do sensor DHT e LED
#define DHTPIN PC1
#define DHTTYPE DHT11
#define LEDGREEN PC12

// Pinos SPI principais
static constexpr int SCK_PIN = PA5;
static constexpr int MISO_PIN = PA6;

static constexpr int MOSI_PIN = PA7;
static constexpr int uSD_CS_PIN = PB6;

// Configurações de depuração e RTC
static constexpr bool IS_DEBUG_PRINT = false;
static constexpr bool IS_DEBUG_LOG = false;   // Define se os logs de depuração estão habilitados
static constexpr bool IS_SERIAL_PRINT = true; // Define se as mensagens serão exibidas no Serial
static constexpr bool IS_RTC_ENABLED = true;
static constexpr bool SET_RTC_BOOT = true;

// Estrutura para armazenar data e hora
struct DateTime
{
    uint8_t day;
    uint8_t month;
    uint16_t year;
    uint8_t hours;
    uint8_t minutes;
    uint8_t seconds;
}; // Adicionado ponto e vírgula aqui

// Propriedades do Logger
static const std::string CONFIG_FILENAME = "config.csv";
static const std::string CONFIG_PATH = "/";
static const std::string LOG_FILENAME = "system";
static const std::string CSV_FILENAME = "data";
static const char *CSV_HEADER = "a,b,c";
static const std::string LOG_PATH = "/logs/";
static constexpr uint32_t SERIAL_BAUDRATE = 115200;
static constexpr uint32_t MAX_FILE_SIZE = 1048576; // Tamanho máximo do arquivo de log em bytes

// Configurações do sistema de arquivos
const uint16_t TIME_BETWEEN_INITS = 1000; // Intervalo entre inicializações (em ms)
const uint8_t FILE_ENTRY_INCREMENT = 3;   // Incremento de entradas de arquivos
const uint8_t FIRST_FILE_ENTRY = 2;       // Entrada do primeiro arquivo (após a tabela de partição)
const uint16_t FAT32_BLOCK_SIZE = 512;    // Tamanho do bloco FAT32

// Estrutura para armazenar dados de configuração
struct configData
{
    char asn[15];
    char sn[15];
    char hw[5];
    char fw[5];
    float S1;
    float S2;
    float S3;
    float S4;
};

// Declaração de variável externa para acessar os dados de configuração
extern configData config_data;

#endif /* CONFIG_HPP_INCLUDED_ */