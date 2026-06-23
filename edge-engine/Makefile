# Compilador e flags
CC = gcc
CFLAGS = -Wall -Wextra -Werror -g -fPIE -Iinclude
LDFLAGS = -lm -lrt

# Diretórios
SRC_DIR = src
OBJ_DIR = obj
BIN_DIR = bin
INCLUDE_DIR = include

# Encontrar todos os arquivos fonte
SRCS = $(wildcard $(SRC_DIR)/*.c) \
       $(wildcard $(SRC_DIR)/parser/*.c) \
       $(wildcard $(SRC_DIR)/executor/*.c) \
       $(wildcard $(SRC_DIR)/builtins/*.c) \
       $(wildcard $(SRC_DIR)/utils/*.c)

# Gerar nomes dos objetos
OBJS = $(patsubst $(SRC_DIR)/%.c,$(OBJ_DIR)/%.o,$(SRCS))

# Nome do executável
TARGET = $(BIN_DIR)/edge-engine

# Regra padrão
all: $(TARGET)

# Criar diretórios
$(OBJ_DIR) $(BIN_DIR):
	mkdir -p $@

# Compilar objetos com flags adicionais
$(OBJ_DIR)/%.o: $(SRC_DIR)/%.c | $(OBJ_DIR)
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

# Linkar
$(TARGET): $(OBJS) | $(BIN_DIR)
	$(CC) $(CFLAGS) $^ -o $@ $(LDFLAGS)
	@echo "Build concluído: $(TARGET)"

# Limpeza
clean:
	rm -rf $(OBJ_DIR) $(BIN_DIR)
	@echo "Limpo."

# Valgrind: roda todos os scripts de teste verificando vazamentos
valgrind: $(TARGET)
	@mkdir -p tests/valgrind_logs
	@for f in tests/test_scripts/*.txt; do \
		name=$$(basename $$f .txt); \
		echo "=== valgrind: $$name ==="; \
		valgrind --leak-check=full --show-leak-kinds=all --track-origins=yes \
		         --trace-children=no --child-silent-after-fork=yes \
		         --error-exitcode=99 \
		         $(TARGET) $$f >/dev/null 2>tests/valgrind_logs/$$name.log; \
		grep -E "in use at exit|ERROR SUMMARY" tests/valgrind_logs/$$name.log \
		     | sed 's/^==[0-9]*== //'; \
	done

# Debug
debug: CFLAGS += -DDEBUG -g -O0
debug: all

# Mostrar estrutura
tree:
	@tree -L 3 -I "obj|bin|*.o"

# Ajuda
help:
	@echo "Comandos disponíveis:"
	@echo "  make          - Compila o projeto"
	@echo "  make clean    - Remove arquivos objetos e binário"
	@echo "  make valgrind - Verifica vazamentos de memória em todos os testes"
	@echo "  make debug    - Compila com flags de debug"
	@echo "  make tree     - Mostra estrutura do projeto"
	@echo "  make help     - Mostra esta mensagem"

.PHONY: all clean valgrind debug tree help
