////////////////////////////////////////////////////////////////////////
// MONITOR DE ESTUFA - tela ILI9341 240x320 (horizontal) + 4 sensores reais
//
// Sensores:
//   - Luz (módulo LDR, saída AO)    -> GPIO 34
//   - Umidade do solo (capacitivo)  -> GPIO 35
//   - DHT11 (temperatura e umidade do ar) -> GPIO 27
//
// Precisa de -DLOAD_GFXFF=1 nas build_flags do platformio.ini
////////////////////////////////////////////////////////////////////////
#include <Arduino.h>
#include <TFT_eSPI.h>
#include <DHT.h>
#define GFXFF 1

TFT_eSPI tft = TFT_eSPI();

////////////////////////////////////////////////////////////////////////
// Sensores
const int PINO_LUZ = 34;
const int PINO_SOLO = 35;

#define PINO_DHT 27
#define TIPO_DHT DHT11
DHT dht(PINO_DHT, TIPO_DHT);

// Calibração do sensor de solo (valores BRUTOS do analogRead, 0 a 4095).
// Estes números são só um ponto de partida: leia o valor no monitor serial
// com o sensor no ar (seco) e dentro de um copo d'água (molhado) e troque aqui.
const int SOLO_SECO = 3200;
const int SOLO_MOLHADO = 1400;

////////////////////////////////////////////////////////////////////////
// Cores (formato RGB565). rgb(r, g, b) converte valores de 0 a 255.
constexpr uint16_t rgb(uint8_t r, uint8_t g, uint8_t b)
{
    return ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3);
}

const uint16_t COR_FUNDO = rgb(15, 17, 23);
const uint16_t COR_HEADER = rgb(24, 28, 38);
const uint16_t COR_CARD = rgb(30, 34, 45);
const uint16_t COR_BARRA_FUNDO = rgb(55, 60, 75);
const uint16_t COR_TEXTO = rgb(235, 238, 245);
const uint16_t COR_TITULO = rgb(150, 158, 175);

const uint16_t COR_VERDE = rgb(76, 200, 120);
const uint16_t COR_AMARELO = rgb(240, 190, 60);
const uint16_t COR_VERMELHO = rgb(230, 80, 80);
const uint16_t COR_LARANJA = rgb(240, 140, 60);
const uint16_t COR_AZUL = rgb(80, 160, 240);

////////////////////////////////////////////////////////////////////////
// Medidas do layout (tela 320 x 240). Mude aqui e o layout todo se ajusta.
const int LARGURA = 320;
const int MARGEM = 8;
const int HEADER_H = 28;
const int CARD_W = 148; // (320 - 3 * 8) / 2
const int CARD_H = 94;  // cabem 2 linhas abaixo do cabeçalho

const int COL1 = MARGEM;
const int COL2 = MARGEM + CARD_W + MARGEM;
const int LIN1 = HEADER_H + MARGEM;
const int LIN2 = LIN1 + CARD_H + MARGEM;

////////////////////////////////////////////////////////////////////////
// Cada cartão: título, unidade, posição e como escolher a cor
struct Cartao
{
    const char *titulo;
    const char *unidade;
    int x;
    int y;
    bool semaforo;    // true: cor muda conforme o valor (vermelho/amarelo/verde)
    uint16_t corFixa; // usada quando semaforo = false
};

enum { LUZ, SOLO, TEMPERATURA, UMIDADE_AR };

Cartao cartoes[4] = {
    {"Luz", "%", COL1, LIN1, false, COR_AMARELO},
    {"Solo", "%", COL2, LIN1, true, COR_VERDE},
    {"Temperatura", " C", COL1, LIN2, false, COR_LARANJA},
    {"Umidade ar", "%", COL2, LIN2, false, COR_AZUL},
};

////////////////////////////////////////////////////////////////////////
// Cor de semáforo: baixo = vermelho, médio = amarelo, alto = verde
uint16_t corSemaforo(int pct)
{
    if (pct < 30)
        return COR_VERMELHO;
    if (pct < 60)
        return COR_AMARELO;
    return COR_VERDE;
}

////////////////////////////////////////////////////////////////////////
// Leitura analógica com média de 10 amostras (o ADC da ESP32 oscila bastante)
int lerAnalogico(int pino)
{
    long soma = 0;
    for (int i = 0; i < 10; i++)
    {
        soma += analogRead(pino);
        delay(2);
    }
    return soma / 10;
}

////////////////////////////////////////////////////////////////////////
// Desenha a parte FIXA da tela. Chamada UMA vez no setup().
void desenharLayout()
{
    tft.fillScreen(COR_FUNDO);

    // Cabeçalho
    tft.fillRect(0, 0, LARGURA, HEADER_H, COR_HEADER);
    tft.setTextPadding(0);      // sem padding para textos fixos
    tft.setTextDatum(ML_DATUM); // alinha pelo meio da esquerda
    tft.setTextColor(COR_TEXTO, COR_HEADER);
    tft.setFreeFont(&FreeSansBold12pt7b);
    tft.drawString("Estufa", 10, HEADER_H / 2, GFXFF);
    tft.fillCircle(LARGURA - 16, HEADER_H / 2, 5, COR_VERDE); // "luzinha" de status

    // Cartões (fundo arredondado + título)
    for (int i = 0; i < 4; i++)
    {
        Cartao &c = cartoes[i];
        tft.fillRoundRect(c.x, c.y, CARD_W, CARD_H, 8, COR_CARD);

        tft.setTextDatum(TL_DATUM); // alinha pelo canto de cima, à esquerda
        tft.setTextColor(COR_TITULO, COR_CARD);
        tft.drawString(c.titulo, c.x + 10, c.y + 8, 2); // fonte 2 (simples)
    }
}

////////////////////////////////////////////////////////////////////////
// Atualiza SÓ o valor e a barra de um cartão (não pisca).
//   valor:  número mostrado no cartão
//   pct:    0 a 100, define o tamanho da barra
//   valido: false mostra "--" (leitura ainda não disponível ou com falha)
void atualizarCartao(const Cartao &c, int valor, int pct, bool valido = true)
{
    pct = constrain(pct, 0, 100);
    uint16_t cor = c.semaforo ? corSemaforo(pct) : c.corFixa;
    String texto = valido ? String(valor) + c.unidade : String("--");
    if (!valido)
        pct = 0;

    // Número grande no centro do cartão
    tft.setFreeFont(&FreeSansBold18pt7b);
    tft.setTextDatum(MC_DATUM);      // alinha pelo centro
    tft.setTextColor(cor, COR_CARD); // o fundo apaga o texto antigo
    tft.setTextPadding(120);         // limpa 120 px de largura antes de escrever
    tft.drawString(texto, c.x + CARD_W / 2, c.y + 44, GFXFF);

    // Barra de progresso
    int bx = c.x + 10;
    int by = c.y + CARD_H - 20;
    int bw = CARD_W - 20;
    int bh = 10;
    int cheio = bw * pct / 100;
    tft.fillRect(bx, by, cheio, bh, cor);                          // parte preenchida
    tft.fillRect(bx + cheio, by, bw - cheio, bh, COR_BARRA_FUNDO); // parte vazia
}

////////////////////////////////////////////////////////////////////////
void setup()
{
    Serial.begin(115200);

    dht.begin();

    tft.init();
    tft.setRotation(1);
    desenharLayout();
}

////////////////////////////////////////////////////////////////////////
void loop()
{
    // Atualiza a cada 1 segundo, sem travar com delay() longo
    static uint32_t ultimo = 0;
    static uint32_t ultimoDht = 0;
    static float temp = NAN; // última leitura válida do DHT11
    static float umAr = NAN;

    if (millis() - ultimo < 1000)
        return;
    ultimo = millis();

    ////////////////////////////////////////////////////////////////////
    // Luz (o valor cai quando tem mais luz, por isso a escala invertida)
    int luzBruto = lerAnalogico(PINO_LUZ);
    int luz = constrain(map(luzBruto, 4095, 0, 0, 100), 0, 100);

    ////////////////////////////////////////////////////////////////////
    // Solo (seco = valor alto, molhado = valor baixo)
    int soloBruto = lerAnalogico(PINO_SOLO);
    int solo = constrain(map(soloBruto, SOLO_SECO, SOLO_MOLHADO, 0, 100), 0, 100);

    ////////////////////////////////////////////////////////////////////
    // DHT11: só lê a cada 2 segundos e guarda a última leitura válida
    if (millis() - ultimoDht >= 2000)
    {
        ultimoDht = millis();
        float t = dht.readTemperature();
        float h = dht.readHumidity();
        if (isnan(t) || isnan(h))
        {
            Serial.println("falha ao ler o DHT11");
        }
        else
        {
            temp = t;
            umAr = h;
        }
    }
    bool dhtOk = !isnan(temp) && !isnan(umAr);

    ////////////////////////////////////////////////////////////////////
    // Monitor serial (para calibrar e conferir)
    Serial.print("luz bruto: ");
    Serial.print(luzBruto);
    Serial.print(" | solo bruto: ");
    Serial.print(soloBruto);
    Serial.print(" | temp: ");
    Serial.print(temp);
    Serial.print(" | umidade ar: ");
    Serial.println(umAr);

    ////////////////////////////////////////////////////////////////////
    // Tela
    atualizarCartao(cartoes[LUZ], luz, luz);
    atualizarCartao(cartoes[SOLO], solo, solo);
    atualizarCartao(cartoes[TEMPERATURA],
                    dhtOk ? (int)round(temp) : 0,
                    dhtOk ? map((int)round(temp), 0, 50, 0, 100) : 0,
                    dhtOk);
    atualizarCartao(cartoes[UMIDADE_AR],
                    dhtOk ? (int)round(umAr) : 0,
                    dhtOk ? (int)round(umAr) : 0,
                    dhtOk);
}