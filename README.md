# Monitor de deposito com ESP8266

Projeto com dois ESP8266: um servidor mede o nivel da agua, temperatura ambiente, umidade e temperatura do boiler; um monitor remoto consulta os dados pela rede local e os mostra em OLED e LEDs WS2812B.

O servidor fornece uma pagina web em `/` e dados JSON em `/data`. O envio ao Google Sheets e feito a cada 5 minutos quando configurado. O endpoint `/rele` comuta o rele pela rede local; nao exponha o ESP8266 diretamente a internet, pois esse endpoint nao possui autenticacao.

## Arquivos

| Pasta/arquivo | Conteudo |
| --- | --- |
| `Servidor/Servidor.ino` | Firmware do servidor, sensores, rele, OLED, LEDs e pagina web |
| `Monitor_Remoto/Monitor_Remoto.ino` | Firmware do monitor remoto com OLED e LEDs |
| `Servidor/config.example.h` | Modelo das credenciais Wi-Fi e ID do Apps Script |
| `Monitor_Remoto/config.example.h` | Modelo das credenciais Wi-Fi e IP do servidor |
| `ligacao-jsn-sr04t-esp8266.svg` | Diagrama de ligacao do sensor ultrassonico |
| `caixa-servidor-3d/` | Modelo parametrico de caixa para OpenSCAD |

## Preparacao

1. Instale o suporte para placas ESP8266 na Arduino IDE e selecione a placa compativel com seu hardware, por exemplo NodeMCU 1.0 (ESP-12E Module).
2. Instale as bibliotecas Adafruit GFX, Adafruit SSD1306, DHT sensor library, Adafruit Unified Sensor, OneWire, DallasTemperature, ArduinoJson e NeoPixelBus.
3. Copie `Servidor/config.example.h` para `Servidor/config.h` e preencha a rede Wi-Fi. Para usar o Google Sheets, preencha tambem `SHEET_SCRIPT_ID` com o ID do seu Apps Script publicado e acessivel ao dispositivo.
4. Copie `Monitor_Remoto/config.example.h` para `Monitor_Remoto/config.h`, preencha a rede Wi-Fi e o IP mostrado pelo servidor.
5. Abra cada arquivo `.ino` na sua propria pasta na Arduino IDE e grave no respectivo ESP8266.

Os arquivos `config.h` ficam fora do Git pelo `.gitignore`. O pacote usa apenas os modelos `config.example.h`.

## Ligacoes do servidor

| Componente | Pino ESP8266 |
| --- | --- |
| JSN-SR04T TRIG | D5 |
| JSN-SR04T ECHO | D6, via divisor de tensao para 3,3 V |
| DHT22 DATA | D4 |
| Rele IN | D3 |
| DS18B20 DATA | D7 |
| WS2812B DIN | D8 |
| OLED SDA / SCL | D2 / D1 |

Alimente o JSN-SR04T conforme a especificacao do seu modulo e mantenha GND comum. Nunca ligue ECHO de 5 V diretamente ao ESP8266. D3 (GPIO0) e D8 (GPIO15) participam da inicializacao da placa; confira se os modulos ligados neles nao impedem o boot. Para o DS18B20, use resistor pull-up de 4,7 kOhm entre DATA e 3,3 V. Confira se o modulo DHT22 ja possui pull-up; se nao, acrescente um entre DATA e 3,3 V.

O monitor remoto usa D1/D2 para OLED e D5 para a fita WS2812B. Use alimentacao adequada para a fita e GND comum com o ESP8266.

## Dados e calibracao

`/data` conserva os nomes de campos usados pelo monitor remoto: `nivel_cm`, `nivel_porcent`, `temp`, `hum`, `temp_boiler` e `rele_on`. Campos adicionais incluem `temp_boiler_raw`, `temp_boiler_offset` e `boiler_suspeito`.

O valor inicial de `TEMP_BOILER_OFFSET` no servidor e `0.0f`. So altere depois de comparar a leitura bruta do DS18B20 com um termometro confiavel **no mesmo ponto fisico**. Diferenca causada por contato ruim, isolamento ou posicao do sensor nao deve ser corrigida automaticamente com um offset fixo. O filtro do boiler pode manter o ultimo valor enquanto avalia uma mudanca grande; consulte `boiler_suspeito` para saber quando isso acontece.

## Caixa 3D

Abra `caixa-servidor-3d/caixa_servidor_esp8266.scad` no OpenSCAD. Ajuste `peca` para `base`, `tampa` ou `montagem` e pressione F5 para visualizar. Para exportar uma peca, use F6 e exporte STL. As medidas sao aproximadas: confira os modulos fisicos antes da impressao final. O modelo nao e vedado nem define uma separacao certificada para tensao de rede no rele.

## Publicacao

Este diretorio foi preparado para um repositorio Git. Revise `git status` antes de enviar e confirme que nenhum `config.h`, arquivo binario ou dado de instalacao foi incluido. Licenca: MIT.
