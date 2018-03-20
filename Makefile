fucked-up: fucked-up.c
	gcc -O3 -Wall fucked-up.c -o fucked-up

install: fucked-up
	install fucked-up /bin/

clean:
	rm fucked-up

tests: fucked-up
	./fucked-up -f tests/helloworld.bf | diff tests/helloworld.result -
	./fucked-up -f tests/fizzbuzz.bf | diff tests/fizzbuzz.result -
	./fucked-up -f tests/mandelbrot.bf | diff tests/mandelbrot.result -
