CC = g++
CFLAGS = -std=c++17 -O2 -Wall
LDFLAGS = -lncurses

SRC = src/main.cpp src/monitor.cpp src/monitor_display.cpp
OBJ = $(SRC:.cpp=.o)

INCLUDE = -Iinclude

all: activity_monitor

activity_monitor: $(SRC)
	$(CC) $(CFLAGS) $(INCLUDE) -o $@ $(SRC) $(LDFLAGS)

clean:
	rm -f activity_monitor $(OBJ)

.PHONY: all clean
