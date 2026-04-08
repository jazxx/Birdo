Files included:
- wserver.c
- request.c
- request.h
- io_helper.c
- io_helper.h
- Makefile
- www/index.html

Build:
    make

Run:
    ./wserver -p 8080 -d ./www

Test on the Pi:
    curl http://localhost:8080/

Open from another device:
    http://<pi-ip>:8080
