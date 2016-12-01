local:
	./travis-tests
.PHONY: local

after_success:
	./travis-after_success
after_failure:
	./travis-after_failure
.PHONY: after_success

%:
	$(MAKE) -C ../build $@
