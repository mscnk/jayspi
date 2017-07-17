jayspi: jayspi.c
	$(CC) $< -Wall `pkg-config --cflags --libs glib-2.0 libjaylink` -o $@
