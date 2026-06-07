## Esp32 web hosting base

It is a base for making websites that run on esp32. It has websockets and html/css/js code integration.
Do what you want with it. I'll do something as well. It is not the best looking right now but it is just a base.

## Requirements

Currently needs:
- at least 4mb of flash size
- mdns component enabled
- http websocket component enabled

It is just a base so thats it.

## Usage

Depends on what you want but most basic would be to put your website fiels inside the spiffs folder and keep it under 1mb in size. You can probably squeeze more space for it by changing the `partitions.csv`.

## Needs

- Cleaning. The main file is a mess. Should make it cleaner and divide it to multiple files. But that could just me.
- Thats it. The rest is up to whatever you want from this.