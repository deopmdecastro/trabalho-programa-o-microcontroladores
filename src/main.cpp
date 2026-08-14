#include <Arduino.h>
#include "config.hpp"

// Protótipo da interrupção
void buttonISR();

// Pinos
const int ledPin = PA5;
const int buttonPin = PC13;

// Variáveis partilhadas com a interrupção
volatile bool buttonPressed = false;

// Estado do LED
bool ledState = false;

// Estado do programa
bool running = false;
bool stopped = false;

// Contador
int contador = 0;

// Timers
unsigned long previousCount = 0;
unsigned long previousBlink = 0;
unsigned long lastButtonTime = 0;

// Controlo do timeout
bool buttonEverPressed = false;
bool timeoutActive = false;

void setup()
{
    Serial.begin(SERIAL_BAUDRATE);

    pinMode(ledPin, OUTPUT);
    pinMode(buttonPin, INPUT_PULLUP);

    digitalWrite(ledPin, LOW);

    attachInterrupt(digitalPinToInterrupt(buttonPin), buttonISR, FALLING);

    Serial.println("=== Comandos ===");
    Serial.println("start - Iniciar");
    Serial.println("pause - Pausar");
    Serial.println("reset - Reset contador");
    Serial.println("stop - Parar programa");
}

void loop()
{
    // Se o programa foi parado, não faz mais nada
    if (stopped)
    {
        while (true)
        {
            // Programa parado definitivamente
        }
    }

    // ==========================
    // Leitura de comandos
    // ==========================
    if (Serial.available())
    {
        String comando = Serial.readStringUntil('\n');
        comando.trim();

        if (comando == "start")
        {
            running = true;
            timeoutActive = false;

            previousCount = millis();
            previousBlink = millis();

            Serial.println("Programa iniciado");
        }
        else if (comando == "pause")
        {
            running = false;

            ledState = false;
            digitalWrite(ledPin, LOW);

            Serial.println("Programa pausado");
        }
        else if (comando == "reset")
        {
            contador = 0;
            previousCount = millis();

            Serial.println("Contador resetado");
        }
        else if (comando == "stop")
        {
            running = false;
            ledState = false;
            digitalWrite(ledPin, LOW);

            stopped = true;

            Serial.println("Programa parado");
        }
    }

    // ==========================
    // Contador
    // ==========================
    if (running && millis() - previousCount >= 1000)
    {
        previousCount += 1000;

        contador++;

        Serial.print("Contador: ");
        Serial.println(contador);
    }

    // ==========================
    // Blink do LED
    // ==========================
    if (running && !timeoutActive && millis() - previousBlink >= 500)
    {
        previousBlink += 500;

        ledState = !ledState;
        digitalWrite(ledPin, ledState);
    }

    // ==========================
    // Botão
    // ==========================
    if (buttonPressed)
    {
        noInterrupts();
        buttonPressed = false;
        interrupts();

        ledState = !ledState;
        digitalWrite(ledPin, ledState);

        lastButtonTime = millis();
        buttonEverPressed = true;
        timeoutActive = false;

        Serial.println("Botao pressionado");
    }

    // ==========================
    // Timeout de 15 segundos
    // ==========================
    if (buttonEverPressed &&
        !timeoutActive &&
        millis() - lastButtonTime >= 15000)
    {
        timeoutActive = true;

        ledState = false;
        digitalWrite(ledPin, LOW);

        Serial.println("15 segundos sem pressionar o botao. LED desligado.");
    }
}

// ==========================
// Interrupção
// ==========================
void buttonISR()
{
    buttonPressed = true;
}