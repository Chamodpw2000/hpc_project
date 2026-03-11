#
# Makefile for Score Analyzer Backend
# Copyright (c) 2026
# MIT License
#

PROG = score_analyzer_backend
SRC = score_analyzer_backend.c
DB_SRC = db.c
CONFIG_SRC = config.c

# Controller sources
CONTROLLER_SRC = controllers/response_helper.c \
                 controllers/health_controller.c \
                 controllers/student_controller.c \
                 controllers/data_controller.c \
                 controllers/calc_engine.c \
                 controllers/score_controller.c \
                 controllers/class_controller.c

CONTROLLER_OBJ = controllers/response_helper.o \
                 controllers/health_controller.o \
                 controllers/student_controller.o \
                 controllers/data_controller.o \
                 controllers/calc_engine.o \
                 controllers/score_controller.o \
                 controllers/class_controller.o

# Local CivetWeb files (self-contained)
CIVETWEB_LIB = libcivetweb.a
CIVETWEB_SRC = src/civetweb.c

# MongoDB flags and libraries
MONGOC_FLAGS = $(shell pkg-config --cflags libmongoc-1.0)
MONGOC_LIBS  = $(shell pkg-config --libs   libmongoc-1.0)

CFLAGS = -I./include $(COPT) -Wall -Wextra -std=c99 $(MONGOC_FLAGS) -D_GNU_SOURCE -fopenmp
LIBS   = -lpthread $(MONGOC_LIBS) -lgomp -lm

include ./resources/Makefile.in-os

ifeq ($(TARGET_OS),LINUX)
	LIBS += -ldl
endif

all: $(PROG)

$(PROG): $(CIVETWEB_LIB) db.o config.o $(CONTROLLER_OBJ) $(SRC)
	$(CC) -o $@ $(CFLAGS) $(LDFLAGS) $(SRC) db.o config.o $(CONTROLLER_OBJ) $(CIVETWEB_LIB) $(LIBS)

db.o: $(DB_SRC) include/db.h
	$(CC) $(CFLAGS) -c $(DB_SRC) -o db.o

config.o: $(CONFIG_SRC) include/config.h
	$(CC) $(CFLAGS) -c $(CONFIG_SRC) -o config.o

controllers/%.o: controllers/%.c
	$(CC) $(CFLAGS) -c $< -o $@

$(CIVETWEB_LIB): $(CIVETWEB_SRC)
	$(CC) $(CFLAGS) -DNDEBUG -DNO_SSL -c -fPIC $(CIVETWEB_SRC) -o civetweb.o
	ar cq $(CIVETWEB_LIB) civetweb.o
	ranlib $(CIVETWEB_LIB)

clean:
	rm -f $(CIVETWEB_LIB) $(PROG) *.o controllers/*.o *.log

run: $(PROG)
	./$(PROG)

test: $(PROG)
	@echo "Starting Score Analyzer Backend in background..."
	./$(PROG) &
	@echo "Server PID: $$!"
	@echo "Use 'make stop' to stop the server"

stop:
	@echo "Stopping Score Analyzer Backend..."
	@pkill -f "./$(PROG)" || echo "No server running"

watch:
	@echo "👀 Watching for changes - Server will auto-restart on file changes"
	@echo "Press Ctrl+C to stop"
	@echo ""
	@-pkill -9 -f "score_analyzer_backend" 2>/dev/null || true
	@sleep 0.5
	@find . -name "*.c" -o -path "./include/*.h" | entr -r make run

install: $(PROG)
	mkdir -p /usr/local/bin
	cp $(PROG) /usr/local/bin/

uninstall:
	rm -f /usr/local/bin/$(PROG)

help:
	@echo "Available targets:"
	@echo "  all      - Build Score Analyzer Backend"
	@echo "  clean    - Remove build files and logs"
	@echo "  run      - Build and start the server"
	@echo "  test     - Start server in background"
	@echo "  stop     - Stop background server"
	@echo "  watch    - Auto-rebuild and restart on file changes (like nodemon)"
	@echo "  install  - Install to /usr/local/bin"
	@echo "  help     - Show this help message"

.PHONY: all clean run test stop watch install uninstall help