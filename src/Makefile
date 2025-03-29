# Название бинарного файла
TARGET = ipban

# Исходный файл
SRC = ipban.c

# Компилятор и флаги
C= clang
LDFLAGS = -static

# Путь к файлу info.toml
INFO_FILE = info.toml

# Сборка
all: $(TARGET) $(INFO_FILE)

# Правила для создания бинарника
$(TARGET): $(SRC)
	$(C) -o $@ $^ $(LDFLAGS)

# Правило для создания файла info.toml
$(INFO_FILE):
	@echo "[platform]" > $(INFO_FILE)
	@echo "os = \"$(shell uname -s)\"" >> $(INFO_FILE)
	@echo "arch = \"$(shell uname -m)\"" >> $(INFO_FILE)
	@echo "" >> $(INFO_FILE)
	@echo "[compiler]" >> $(INFO_FILE)
	@echo "name = \"$(shell $(C) --version | head -n 1)\"" >> $(INFO_FILE)
	@echo "version = \"$(shell $(C) --version | head -n 1 | cut -d' ' -f3)\"" >> $(INFO_FILE)
	@echo "" >> $(INFO_FILE)
	@echo "[build]" >> $(INFO_FILE)
	@echo "date = \"$(shell date '+%Y-%m-%d %H:%M:%S')\"" >> $(INFO_FILE)

# Очистка
clean:
	rm -f $(TARGET) $(INFO_FILE)
