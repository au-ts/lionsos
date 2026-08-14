# Object file
OBJS_LIBVSPACE := $(LIBVSPACE_DIR)/vspace.o

# Rule to compile the object
$(LIBVSPACE_DIR)/vspace.o: $(LIBVSPACE_DIR)/vspace.c
	$(CC) -c $(CFLAGS_reloader) $(LIBVSPACE_DIR)/vspace.c -o $(LIBVSPACE_DIR)/vspace.o

# Rule to create the archive
$(LIBVSPACE_DIR)/libvspace.a: $(OBJS_LIBVSPACE)
	$(AR) crv $@ $^
	$(RANLIB) $@


libvspace:
	mkdir -p $@

clean::
	${RM} -f ${ALL_OBJS_LIBVSPACE} ${ALL_OBJS_LIBVSPACE:.o=.d}

clobber:: clean
	${RM} -f libvspace.a
	rmdir libvspace

-include ${ALL_OBJS_LIBVSPACE:.o=.d}