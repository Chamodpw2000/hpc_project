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
                 controllers/class_controller.c \
                 controllers/correlation_engine.c \
                 controllers/correlation_controller.c

CONTROLLER_OBJ = controllers/response_helper.o \
                 controllers/health_controller.o \
                 controllers/student_controller.o \
                 controllers/data_controller.o \
                 controllers/calc_engine.o \
                 controllers/score_controller.o \
                 controllers/class_controller.o \
                 controllers/correlation_engine.o \
                 controllers/correlation_controller.o

# Local CivetWeb files (self-contained)
CIVETWEB_LIB = libcivetweb.a
CIVETWEB_SRC = src/civetweb.c

# MongoDB flags and libraries
MONGOC_FLAGS = $(shell pkg-config --cflags libmongoc-1.0)
MONGOC_LIBS  = $(shell pkg-config --libs   libmongoc-1.0)

# libcurl for HTTP requests (randomuser.me API)
CURL_FLAGS = $(shell pkg-config --cflags libcurl)
CURL_LIBS  = $(shell pkg-config --libs   libcurl)

CFLAGS = -I./include $(COPT) -Wall -Wextra -std=c99 $(MONGOC_FLAGS) $(CURL_FLAGS) -D_GNU_SOURCE -fopenmp
LIBS   = -lpthread $(MONGOC_LIBS) $(CURL_LIBS) -lgomp -lm

include ./resources/Makefile.in-os

ifeq ($(TARGET_OS),LINUX)
	LIBS += -ldl
endif

# MPI setup variables
MPICC = mpicc
MPI_CFLAGS = $(CFLAGS) -DENABLE_MPI
MPI_SRC = controllers/calc_mpi.c \
          controllers/correlation_mpi.c \
          controllers/mpi_worker.c
MPI_OBJ = controllers/calc_mpi.o \
          controllers/correlation_mpi.o \
          controllers/mpi_worker.o
MPI_CTRL_OBJ = $(subst controllers/,mpi-controllers/,$(CONTROLLER_OBJ))

all: $(PROG)

$(PROG): $(CIVETWEB_LIB) mpi-db.o mpi-config.o $(MPI_CTRL_OBJ) $(MPI_OBJ) $(SRC)
	$(MPICC) -o $@ $(MPI_CFLAGS) $(LDFLAGS) \
		$(SRC) mpi-db.o mpi-config.o $(MPI_CTRL_OBJ) $(MPI_OBJ) \
		$(CIVETWEB_LIB) $(LIBS)
	@echo ""
	@echo "✓ Build complete: $(PROG) (MPI-enabled)"
	@echo "  Run with: mpirun --allow-run-as-root -np 4 ./$(PROG)"

serial: $(PROG)_serial

$(PROG)_serial: $(CIVETWEB_LIB) db.o config.o $(CONTROLLER_OBJ) $(SRC)
	$(CC) -o $@ $(CFLAGS) $(LDFLAGS) $(SRC) db.o config.o $(CONTROLLER_OBJ) $(CIVETWEB_LIB) $(LIBS)
	@echo ""
	@echo "✓ Build complete: $@ (Serial + OpenMP)"

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
	rm -f $(CIVETWEB_LIB) $(PROG) $(PROG)_serial *.o controllers/*.o mpi-db.o mpi-config.o $(MPI_OBJ) *.log
	rm -rf mpi-controllers

run: $(PROG)
	mpirun --allow-run-as-root -np 4 ./$(PROG)

run-serial: $(PROG)
	./$(PROG)

test: $(PROG)
	@echo "Starting Score Analyzer Backend in background..."
	mpirun --allow-run-as-root -np 4 ./$(PROG) &
	@echo "Server PID: $$!"
	@echo "Use 'make stop' to stop the server"

stop:
	@echo "Stopping Score Analyzer Backend..."
	@-pkill -9 -f "./$(PROG)" 2>/dev/null || true
	@-pkill -9 -f "./$(PROG)_serial" 2>/dev/null || true

watch:
	@echo "👀 Watching for changes - Server will auto-restart on file changes"
	@echo "Press Ctrl+C to stop"
	@echo ""
	@-pkill -9 -f "score_analyzer_backend" 2>/dev/null || true
	@sleep 0.5
	@find . -name "*.c" -o -path "./include/*.h" | entr -r make run

# ── MPI build targets ────────────────────────────────────────────────────

controllers/calc_mpi.o: controllers/calc_mpi.c controllers/calc_mpi.h controllers/calc_engine.h
	$(MPICC) $(MPI_CFLAGS) -c $< -o $@

controllers/correlation_mpi.o: controllers/correlation_mpi.c controllers/correlation_mpi.h
	$(MPICC) $(MPI_CFLAGS) -c $< -o $@

controllers/mpi_worker.o: controllers/mpi_worker.c controllers/mpi_worker.h
	$(MPICC) $(MPI_CFLAGS) -c $< -o $@

# Recompile standard sources with -DENABLE_MPI using mpicc
mpi-db.o: $(DB_SRC) include/db.h
	$(MPICC) $(MPI_CFLAGS) -c $(DB_SRC) -o mpi-db.o

mpi-config.o: $(CONFIG_SRC) include/config.h
	$(MPICC) $(MPI_CFLAGS) -c $(CONFIG_SRC) -o mpi-config.o



mpi-controllers/%.o: controllers/%.c
	@mkdir -p mpi-controllers
	$(MPICC) $(MPI_CFLAGS) -c $< -o $@

# Backwards compatibility / aliases
mpi: $(PROG)
mpi-run: run
mpi-clean: clean

install: $(PROG)
	mkdir -p /usr/local/bin
	cp $(PROG) /usr/local/bin/

uninstall:
	rm -f /usr/local/bin/$(PROG)

help:
	@echo "Available targets:"
	@echo "  all       - Build Score Analyzer Backend (MPI-enabled by default)"
	@echo "  serial    - Build standard Score Analyzer Backend (Serial + OpenMP only)"
	@echo "  clean     - Remove build files and logs"
	@echo "  run       - Start MPI backend with 4 processes"
	@echo "  run-serial- Run standard/serial backend directly"
	@echo "  test      - Start MPI backend in background"
	@echo "  stop      - Stop background server"
	@echo "  watch     - Auto-rebuild and restart on file changes"
	@echo "  install   - Install to /usr/local/bin"
	@echo "  help      - Show this help message"

.PHONY: all serial clean run run-serial test stop watch mpi mpi-run mpi-clean install uninstall help