# Compilador e flags
CC = gcc
CFLAGS = -Wall -Wextra -Werror -g -fPIE -Iinclude
LDFLAGS = -lm

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
	@echo "Limpeza concluída"

# Testes
test: $(TARGET)
	@echo "Executando testes..."
	@echo "--- Teste 1: ls -la ---"
	@echo "ls -la" | $(TARGET)
	@echo ""
	@echo "--- Teste 2: pwd ---"
	@echo "pwd" | $(TARGET)
	@echo ""
	@echo "--- Teste 3: echo ---"
	@echo 'echo "Hello Edge Engine!"' | $(TARGET)
	@echo ""
	@echo "--- Teste 4: Comandos built-in ---"
	@echo -e "cd /tmp\npwd\ncd -\npwd" | $(TARGET)

# Valgrind (se tiver script de teste)
valgrind: $(TARGET)
	@echo "Verificando com valgrind..."
	@if [ -f tests/test_scripts/test1.txt ]; then \
		valgrind --leak-check=full --show-leak-kinds=all \
		         --track-origins=yes --verbose \
		         $(TARGET) tests/test_scripts/test1.txt 2>&1 | tee valgrind.log; \
	else \
		echo "Arquivo de teste não encontrado. Criando um..."; \
		mkdir -p tests/test_scripts; \
		echo "ls -la" > tests/test_scripts/test1.txt; \
		echo "pwd" >> tests/test_scripts/test1.txt; \
		echo "echo 'Hello'" >> tests/test_scripts/test1.txt; \
		valgrind --leak-check=full --show-leak-kinds=all \
		         --track-origins=yes --verbose \
		         $(TARGET) tests/test_scripts/test1.txt 2>&1 | tee valgrind.log; \
	fi

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
	@echo "  make test     - Executa testes básicos"
	@echo "  make valgrind - Verifica vazamentos de memória"
	@echo "  make debug    - Compila com flags de debug"
	@echo "  make tree     - Mostra estrutura do projeto"
	@echo "  make help     - Mostra esta mensagem"

.PHONY: all clean test valgrind debug tree help
