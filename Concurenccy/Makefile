# Variables
CC = gcc
CFLAGS = -Wall -Werror -pedantic -std=gnu18
LOGIN = bgoh
PROJECT = P6
SUBMITPATH = ~cs537-1/handin/$(LOGIN)/$(PROJECT)
TESTPATH = ~cs537-1/tests/P6

# Set up for next project
nset-up: 
	echo "Not implemented"

# Compile
obscure: runOwnTest.c
	$(CC) $(CFLAGS) -o $@ $^

# Test
test-readme:
	cat $(TESTPATH)/README


# Playground
pg-create:
	touch playground.c

pg-compile:
	gcc -Wall -Werror -o playground playground.c 

pg-run: pg-compile
	./playground

pg-cr: pg-compile pg-run

pg-clean:
	rm playground

pg-clear:
	cat playground.c
	rm playground.c

pg-cleanall: pg-clean pg-clear

# Submit
submit-slipdays:
	cp slipdays.txt $(SUBMITPATH)

submit-readme:
	cp README.md $(SUBMITPATH)

showsp:
	ls $(SUBMITPATH)

clean:
	# echo "I think its clean"
	./p6-main/clean.sh 


# XV6 specific
# This is not tested
# Not sure how to automate make clean in xv6-public
xv6-add:
	cp ~cs537-1/xv6/xv6.tar.gz .

xv6-unpack:
	tar -xzf xv6.tar.gz -C .

xv6-submit:
	cp -r ./xv6-public/. $(SUBMITPATH)

xv6-submitpath-del:
	rm -rf ~cs537-1/handin/$(LOGIN)/$(PROJECT)/xv6-public/

# Run Test
# ~cs537-1/tests/P6/runtests
test-rmv:
	rm -rf ./testing

test1:
	curl 'http://localhost:33489/1/dummy1.html'

test2:
	curl 'http://localhost:33489/1/dummy1.html'
	curl 'http://localhost:33490/1/dummy1.html'

test3:
	curl 'http://localhost:33489/1/dummy1.html'
	curl 'http://localhost:33491/8/dummy1.html'

test4:
	curl 'http://localhost:33489/GetJob'