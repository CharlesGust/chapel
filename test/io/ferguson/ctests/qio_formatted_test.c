#include "qio_formatted.h"
#include <assert.h>

void test_endian(void)
{
  // We write (hex) 00 0102 03040506 0708091011121314
  // as big endian and little endian, and then we check
  // that the data is what we expected.
  
  err_t err;
  qio_file_t* f;
  qio_channel_t* writing;
  qio_channel_t* reading;
  int64_t offset;
  int len = 15;
  uint8_t n0 = 0;
  uint16_t n1 = 0x0102;
  uint32_t n2 = 0x03040506;
  uint64_t n3 = 0x0708091011121314LL;
  const char* expect_le = "\x00\x02\x01\x06\x05\x04\x03"
                          "\x14\x13\x12\x11\x10\x09\x08\x07";
  const char* expect_be = "\x00\x01\x02\x03\x04\x05\x06"
                          "\x07\x08\x09\x10\x11\x12\x13\x14";
  char got[16];
  int i;
  int b_order;
  const char* expect;

  printf("Testing endian functions\n");

  err = qio_file_open_tmp(&f, 0, NULL);
  assert(!err);

  for( i = 0; i < 2; i++ ) {
    if( i == 0 ) {
      b_order = QIO_BIG;
      expect = expect_be;
    } else {
      b_order = QIO_LITTLE;
      expect = expect_le;
    }

    // Create a "write to file" channel.
    err = qio_channel_create(&writing, f, QIO_CH_BUFFERED, 0, 1, 0, INT64_MAX, NULL);
    assert(!err);

    err = qio_channel_write_uint8(true, writing, n0);
    assert(!err);
    err = qio_channel_write_uint16(true, b_order, writing, n1);
    assert(!err);
    err = qio_channel_write_uint32(true, b_order, writing, n2);
    assert(!err);
    err = qio_channel_write_uint64(true, b_order, writing, n3);
    assert(!err);

    err = qio_channel_offset(true, writing, &offset);
    assert(!err);

    assert( offset == len );

    qio_channel_release(writing);
    writing = NULL;

    // Create a "read from file" channel.
    err = qio_channel_create(&reading, f, QIO_CH_BUFFERED, 1, 0, 0, INT64_MAX, NULL);
    assert(!err);

    err = qio_channel_read_amt(true, reading, got, len);
    assert(!err);

    // Check that we read back what we wrote.
    assert( 0 == memcmp(got, expect, len) );

    qio_channel_release(reading);
    reading = NULL;
  }

  // Close the file.
  qio_file_release(f);
  f = NULL;


}

void test_readwriteint(void)
{
  err_t err;
  qio_file_t* f;
  qio_channel_t* writing;
  qio_channel_t* reading;
  const char* testdata[] = {"\x00\x00\x00\x00\x00\x00\x00\x00",
                            "\xff\xff\xff\xff\xff\xff\xff\xff",
                            "\x7f\x7f\x7f\x7f\x7f\x7f\x7f\x7f",
                            "\x01\x02\x03\x04\x05\x06\x07\x08",
                            "\xf1\xf2\xf3\xf4\xf5\xf6\xf7\xf8",
                            NULL };
  int sizes[] = {1, -1, 2, -2, 4, -4, 8, -8, 0};
  int byteorder[] = {QIO_NATIVE, QIO_BIG, QIO_LITTLE, 0};
  char got[16];
  int i,j,k;

  printf("Testing binary integer I/O\n");

  // Open a temporary file.
  err = qio_file_open_tmp(&f, 0, NULL);
  assert(!err);

  for( i = 0; testdata[i]; i++ ) {
    for( j = 0; sizes[j]; j++ ) {
      for( k = 0; byteorder[k]; k++ ) {
        const char* data = testdata[i];
        int sz = sizes[j];
        int b_order = byteorder[k];
        int len;

        len = sz;
        if( len < 0 ) len = - sz;


        memset(got, 0, sizeof(got));

        // Create a "write to file" channel.
        err = qio_channel_create(&writing, f, QIO_CH_BUFFERED, 0, 1, 0, INT64_MAX, NULL);
        assert(!err);

        err = qio_channel_write_int(true, b_order, writing, data, len, sz < 0);
        assert(!err);

        qio_channel_release(writing);
        writing = NULL;

        // Create a "read from file" channel.
        err = qio_channel_create(&reading, f, QIO_CH_BUFFERED, 1, 0, 0, INT64_MAX, NULL);
        assert(!err);


        err = qio_channel_read_int(true, b_order, reading, got, len, sz < 0);
        assert(!err);

        // Check that we read back what we wrote.
        assert( 0 == memcmp(got, data, len) );

        qio_channel_release(reading);
        reading = NULL;
      }
    }
  }

  // Close the file.
  qio_file_release(f);
  f = NULL;

}

void test_printscan_int(void)
{
  err_t err;
  qio_file_t* f;
  qio_channel_t* writing;
  qio_channel_t* reading;

#define NSTYLES 10
  qio_style_t styles[NSTYLES];

  const char* zero[] = { // writing 0
                        "0", // default style
                        "  0", // min_width = 3, pad = ' '
                        "000", // min_width = 3, pad = 0
                        "0  ", // min_width = 3, pad = ' ', leftjustify
                        "0", // hex
                        "0x0", // hex, showbase
                        "   0", // binary, min_width = 4
                        "0b0", // binary, showbase
                        " 0b0", // binary, showbase, showplus=pad
                        "+0X0" // hex, showbase, showplus=+, uppercase
                       };

  const char* one[] = { // writing 1
                        "1", // default style
                        "  1", // min_width = 3, pad = ' '
                        "001", // min_width = 3, pad = 0
                        "1  ", // min_width = 3, pad = ' ', leftjustify
                        "1", // hex
                        "0x1", // hex, showbase
                        "   1", // binary, min_width = 4
                        "0b1", // binary, showbase
                        " 0b1", // binary, showbase, showplus=pad
                        "+0X1" // hex, showbase, showplus=+, uppercase
                       };
  const char* hundred[] = { // writing 100 = 0x64 = 0b1100100
                        "100", // default style
                        "100", // min_width = 3, pad = ' '
                        "100", // min_width = 3, pad = 0
                        "100", // min_width = 3, pad = ' ', leftjustify
                        "64", // hex
                        "0x64", // hex, showbase
                        "1100100", // binary, min_width = 4
                        "0b1100100", // binary, showbase
                        " 0b1100100", // binary, showbase, showplus=pad
                        "+0X64" // hex, showbase, showplus=+, uppercase
                          };
  const char* big[] = { // writing 2^63-1 = 9223372036854775807 = 0x7FFFFFFFFFFFFFFF = 0b111111111111111111111111111111111111111111111111111111111111111
                        "9223372036854775807", // default style
                        "9223372036854775807", // min_width = 3, pad = ' '
                        "9223372036854775807", // min_width = 3, pad = 0
                        "9223372036854775807", // min_width = 3, pad = ' ', leftjustify
                        "7fffffffffffffff", // hex
                        "0x7fffffffffffffff", // hex, showbase
                        "111111111111111111111111111111111111111111111111111111111111111", // binary, min_width = 4
                        "0b111111111111111111111111111111111111111111111111111111111111111", // binary, showbase
                        " 0b111111111111111111111111111111111111111111111111111111111111111", // binary, showbase, showplus=pad
                        "+0X7FFFFFFFFFFFFFFF" // hex, showbase, showplus=+, uppercase
                          };
  const char* small[] = { // writing -2^63 = -9223372036854775808 = -0x8000000000000000 = -0b1000000000000000000000000000000000000000000000000000000000000000
                        "-9223372036854775808", // default style
                        "-9223372036854775808", // min_width = 3, pad = ' '
                        "-9223372036854775808", // min_width = 3, pad = 0
                        "-9223372036854775808", // min_width = 3, pad = ' ', leftjustify
                        "-8000000000000000", // hex
                        "-0x8000000000000000", // hex, showbase
                        "-1000000000000000000000000000000000000000000000000000000000000000", // binary, min_width = 4
                        "-0b1000000000000000000000000000000000000000000000000000000000000000", // binary, showbase
                        "-0b1000000000000000000000000000000000000000000000000000000000000000", // binary, showbase, showplus=pad
                        "-0X8000000000000000" // hex, showbase, showplus=+, uppercase
                          };

  int64_t nums[] = {0, 1, 100, INT64_MAX, INT64_MIN, 0};
  const char** expect_arr[] = { zero, one, hundred, big, small, NULL};
  char got[500];
  char sep[4] = {0,0,0,0};
  ssize_t amt_read;
  int i,j;

  printf("Testing text integer I/O\n");

  for( i = 0; i < NSTYLES; i++ ) {
    qio_style_init_default(&styles[i]);
  }
  // Set up the styles
  // 0 is default.

  // 1 has minwidth 3, pad ' '
  styles[1].min_width = 3;
  styles[1].pad_char = ' ';

  // 2 has minwidth 3, pad 0
  styles[2].min_width = 3;
  styles[2].pad_char = '0';

  // 3 has minwidth 3, pad ' ', leftjustify
  styles[3].min_width = 3;
  styles[3].pad_char = ' ';
  styles[3].leftjustify = 1;

  // 4 has defaults, hex
  styles[4].base = 16;
  styles[4].prefix_base = 0;

  // 5 has defaults, hex, showbase
  styles[5].base = 16;
  styles[5].prefix_base = 1;

  // 6 has defaults, binary, min_width=4
  styles[6].base = 2;
  styles[6].min_width = 4;
  styles[6].prefix_base = 0;

  // 7 has defaults, binary, showbase
  styles[7].base = 2;
  styles[7].prefix_base = 1;

  // 8 has showbase, showplus = pad, binary
  styles[8].base = 2;
  styles[8].prefix_base = 1;
  styles[8].showplus = 2;

  // 9 has showbase, showplus, hex, uppercase
  styles[9].base = 16;
  styles[9].prefix_base = 1;
  styles[9].showplus = 1;
  styles[9].uppercase = 1;

  // Open a temporary file.
  err = qio_file_open_tmp(&f, 0, NULL);
  assert(!err);

  for( i = 0; expect_arr[i]; i++ ) {
    for( j = 0; j < NSTYLES; j++ ) {
      const char* expect = expect_arr[i][j];
      int64_t num = nums[i];
      qio_style_t* style = &styles[j];
      int64_t got_num;

      //printf("Expect '%s' testing 0x%" PRIx64 " style %i\n", expect, num, j);

      memset(got, 0, sizeof(got));

      // Create a "write to file" channel.
      err = qio_channel_create(&writing, f, QIO_CH_BUFFERED, 0, 1, 0, INT64_MAX, style);
      assert(!err);

      err = qio_channel_print_int(true, writing, &num, 8, true);
      assert(!err);

      // write a separator so we can use the same file..
      err = qio_channel_write_amt(true, writing, sep, sizeof(sep));
      assert(!err);

      qio_channel_release(writing);
      writing = NULL;

      // Create a "read from file" channel.
      err = qio_channel_create(&reading, f, QIO_CH_BUFFERED, 1, 0, 0, INT64_MAX, NULL);
      assert(!err);

      err = qio_channel_read(true, reading, got, sizeof(got), &amt_read);
      assert(err == EEOF);

      //printf("Got    '%s' expect '%s'\n", got, expect);
      //
      assert( 0 == strcmp(got, expect) );

      qio_channel_release(reading);
      reading = NULL;


      // Try scanning our number.
      // Create a "read from file" channel.
      err = qio_channel_create(&reading, f, QIO_CH_BUFFERED, 1, 0, 0, INT64_MAX, style);
      assert(!err);

      got_num = 0;
      err = qio_channel_scan_int(true, reading, &got_num, 8, 1);
      assert(!err);

      //printf("Got    '%s'         0x%" PRIx64 "\n", got, got_num);
      assert( got_num == num );

      qio_channel_release(reading);
      reading = NULL;
    }
  }

  // Close the file.
  qio_file_release(f);
  f = NULL;
#undef NSTYLES
}

void test_printscan_float(void)
{
  err_t err;
  qio_file_t* f;
  qio_channel_t* writing;
  qio_channel_t* reading;

#define NSTYLES 9
  qio_style_t styles[NSTYLES];

  const char* zero[] = { // writing 0
                        "0.0", // default style
                        "0.000", // %f precision 3
                        "0.000e+00", // %e precision 3
                        "+0.00000", // showpoint, showplus
                        "0x0p+0", // hex
                        "0X0.000P+0", // hex, uppercase, showpoint, prec 3
                        "0", // %g, 4 significant digits
                        "0.0000", // %f, showpoint, precision 4
                        "0.0000e+00", // %e, showpoint, precision 4
                       };

  const char* one[] = { // writing 1
                        "1.0", // default style
                        "1.000", // %f precision 3
                        "1.000e+00", // %e precision 3
                        "+1.00000", // showpoint, showplus
                        "0x1p+0", // hex
                        "0X1.000P+0", // hex, uppercase, showpoint, prec 3
                        "1", // %g, 4 significant digits
                        "1.0000", // %f, showpoint, precision 4
                        "1.0000e+00", // %e, showpoint, precision 4
                       };

  const char* plusinf[] = { // writing +infinity
                        "inf", // default style
                        "inf", // %f precision 3
                        "inf", // %e precision 3
                        "+inf", // showpoint, showplus
                        "inf", // hex
                        "INF", // hex, uppercase, showpoint, prec 3
                        "inf", // %g, 4 significant digits
                        "inf", // %f, showpoint, precision 4
                        "inf", // %e, showpoint, precision 4
                       };
  const char* minusinf[] = { // writing +infinity
                        "-inf", // default style
                        "-inf", // %f precision 3
                        "-inf", // %e precision 3
                        "-inf", // showpoint, showplus
                        "-inf", // hex
                        "-INF", // hex, uppercase, showpoint, prec 3
                        "-inf", // %g, 4 significant digits
                        "-inf", // %f, showpoint, precision 4
                        "-inf", // %e, showpoint, precision 4
                       };
  const char* nan[] = { // writing nan
                        "nan", // default style
                        "nan", // %f precision 3
                        "nan", // %e precision 3
                        "+nan", // showpoint, showplus
                        "nan", // hex
                        "NAN", // hex, uppercase, showpoint, prec 3
                        "nan", // %g, 4 significant digits
                        "nan", // %f, showpoint, precision 4
                        "nan", // %e, showpoint, precision 4
                       };
  const char* x[] = { // writing 1.125e+300
                        "1.125e+300", // default style
                        "1124999999999999984717009863215819639889402246251651042717325796981054812448928754462634938945285169790751774504176833459124149130131831874871128930639759966162906545922666490056516990646142904182469580674455306426256469801266735686696548991733655898546719119989659797590624855449025294035374384873472.000", // %f precision 3
                        "1.125e+300", // %e precision 3
                        "+1.12500e+300", // showpoint, showplus
                        "0x1.ae0c41900844fp+996", // hex
                        "0X1.AE1P+996", // hex, uppercase, showpoint, prec 3
                        "1.125e+300", // %g, 4 significant digits
                        "1124999999999999984717009863215819639889402246251651042717325796981054812448928754462634938945285169790751774504176833459124149130131831874871128930639759966162906545922666490056516990646142904182469580674455306426256469801266735686696548991733655898546719119989659797590624855449025294035374384873472.0000", // %f, showpoint, precision 4
                        "1.1250e+300", // %e, showpoint, precision 4
                       };
  const char* y[] = { // writing 6.125e-300,
                        "6.125e-300", // default style
                        "0.000", // %f precision 3
                        "6.125e-300", // %e precision 3
                        "+6.12500e-300", // showpoint, showplus
                        "0x1.0685051469a5p-994", // hex
                        "0X1.068P-994", // hex, uppercase, showpoint, prec 3
                        "6.125e-300", // %g, 4 significant digits
                        "0.0000", // %f, showpoint, precision 4
                        "6.1250e-300", // %e, showpoint, precision 4
                       };
  const char* large[] = { // writing 1.7976931348623157e+308
                        "1.79769e+308", // default style
                        "179769313486231570814527423731704356798070567525844996598917476803157260780028538760589558632766878171540458953514382464234321326889464182768467546703537516986049910576551282076245490090389328944075868508455133942304583236903222948165808559332123348274797826204144723168738177180919299881250404026184124858368.000", // %f precision 3
                        "1.798e+308", // %e precision 3
                        "+1.79769e+308", // showpoint, showplus
                        "0x1.fffffffffffffp+1023", // hex
                        "0X2.000P+1023", // hex, uppercase, showpoint, prec 3
                        "1.798e+308", // %g, 4 significant digits
                        "179769313486231570814527423731704356798070567525844996598917476803157260780028538760589558632766878171540458953514382464234321326889464182768467546703537516986049910576551282076245490090389328944075868508455133942304583236903222948165808559332123348274797826204144723168738177180919299881250404026184124858368.0000", // %f, showpoint, precision 4
                        "1.7977e+308", // %e, showpoint, precision 4
                       };
  const char* small[] = { // writing 2.2250738585072014e-308
                        "2.22507e-308", // default style
                        "0.000", // %f precision 3
                        "2.225e-308", // %e precision 3
                        "+2.22507e-308", // showpoint, showplus
                        "0x1p-1022", // hex
                        "0X1.000P-1022", // hex, uppercase, showpoint, prec 3
                        "2.225e-308", // %g, 4 significant digits
                        "0.0000", // %f, showpoint, precision 4
                        "2.2251e-308", // %e, showpoint, precision 4
                       };
  double nums[] = {0.0, 1.0,
                   1.0/0.0 /*+inf*/, -1.0/0.0 /*-inf*/, 0.0*(1.0/0.0), /*nan*/
                   1.125e+300, 6.125e-300,
                   1.7976931348623157e+308, 2.2250738585072014e-308 };

  const char** expect_arr[] = { zero, one, plusinf, minusinf, nan,
                                x,y,large,small, NULL };
  char got[500];
  char sep[4] = {0,0,0,0};
  ssize_t amt_read;
  int i,j;

  printf("Testing text float I/O\n");

  for( i = 0; i < NSTYLES; i++ ) {
    qio_style_init_default(&styles[i]);
    styles[i].showpointzero = 0;
  }
  // Set up the styles
  // 0 is default.
  styles[0].showpointzero = 1;

  // 1 has %f, precision 3
  styles[1].precision = 3;
  styles[1].realtype = 1;

  // 2 has %e, precision 3
  styles[2].precision = 3;
  styles[2].realtype = 2;

  // 3 has showpoint, showplus
  styles[3].showpoint = 1;
  styles[3].showplus = 1;

  // 4 has base 16
  styles[4].base = 16;

  // 5 has base 16, uppercase
  styles[5].base = 16;
  styles[5].uppercase = 1;
  styles[5].showpoint = 1;
  styles[5].precision = 3;

  // 6 %g, 4 significant digits
  styles[6].significant_digits = 4;
  styles[6].realtype = 0;

  // 7 has %f showpoint, precision 4
  styles[7].showpoint = 1;
  styles[7].precision = 4;
  styles[7].realtype = 1;

  // 7 has %e showpoint, precision 4
  styles[8].showpoint = 1;
  styles[8].precision = 4;
  styles[8].realtype = 2;


  // Open a temporary file.
  err = qio_file_open_tmp(&f, 0, NULL);
  assert(!err);

  for( i = 0; expect_arr[i]; i++ ) {
    for( j = 0; j < NSTYLES; j++ ) {
      const char* expect = expect_arr[i][j];
      double num = nums[i];
      qio_style_t* style = &styles[j];
      double got_num;

      if( isnan(num) && signbit(num) ) num = - num;

      //printf("Expect '%s' testing %e style %i\n", expect, num, j);

      memset(got, 0, sizeof(got));

      // Create a "write to file" channel.
      err = qio_channel_create(&writing, f, QIO_CH_BUFFERED, 0, 1, 0, INT64_MAX, style);
      assert(!err);

      err = qio_channel_print_float(true, writing, &num, 8);
      assert(!err);

      // write a separator so we can use the same file..
      err = qio_channel_write_amt(true, writing, sep, sizeof(sep));
      assert(!err);

      qio_channel_release(writing);
      writing = NULL;

      // Create a "read from file" channel.
      err = qio_channel_create(&reading, f, QIO_CH_BUFFERED, 1, 0, 0, INT64_MAX, style);
      assert(!err);

      err = qio_channel_read(true, reading, got, sizeof(got), &amt_read);
      assert(err == EEOF);

      //printf("Got    '%s'\n", got);

      assert( 0 == strcmp(got, expect) );

      qio_channel_release(reading);
      reading = NULL;


      // Try scanning our number.
      // Create a "read from file" channel.
      if( i < 7 ) {
        err = qio_channel_create(&reading, f, QIO_CH_BUFFERED, 1, 0, 0, INT64_MAX, NULL);
        assert(!err);

        got_num = 0;
        err = qio_channel_scan_float(true, reading, &got_num, 8);
        assert(!err);

        //printf("Got    '%s'         %e\n", got, got_num);
        //assert( got_num == num );

        qio_channel_release(reading);
        reading = NULL;
      }
    }
  }

  // Close the file.
  qio_file_release(f);
  f = NULL;

#undef NSTYLES
}

void test_verybasic()
{
	qio_file_t *f = NULL;
	qio_channel_t *writing = NULL;
	qio_channel_t *reading = NULL;
	err_t err; 
	char buf[4] = {0};
	
	//open the tmp file, create a write channel, write our data, and release the channel
  	err = qio_file_open_tmp(&f, 0, NULL);
        assert(!err);
      	err = qio_channel_create(&writing, f, QIO_CH_BUFFERED, 0, 1, 0, INT64_MAX, NULL);
        assert(!err);
      	err = qio_channel_write_amt(true, writing, "\xDE\xAD\xBE\xEF", 4);
        assert(!err);
        qio_channel_release(writing);

	//open a read channel to the tmp file, and read back the data.
    	err = qio_channel_create(&reading, f, QIO_CH_BUFFERED, 1, 0, 0, INT64_MAX, NULL);
        assert(!err);
      	err = qio_channel_read_amt(true, reading, buf, 4);
        assert(!err);
        qio_channel_release(reading);

	///check that what we wrote is what we read back
	if(memcmp(buf, "\xDE\xAD\xBE\xEF", 4) != 0){ assert(0);}

	//close the file
	qio_file_release(f);
	f = NULL;

	printf("PASS: test_verybasic\n");
}

typedef struct string_test_s{
	char *string;
	int length;
}string_test_t;

void string_escape_tests()
{
	qio_style_t style = qio_style_default();
	qio_channel_t *reading;
	qio_channel_t *writing;
	qio_file_t *f = NULL;
	err_t err;
        const char* inputs[2][4] = { {"a \"\\b\x01", // original string
                                      "\"a \\\"\\\\b\x01\"", // simple encoding
                                      "\"a \\\"\\\\b\\x01\"", // chapel encoding
                                      "\"a \\\"\\\\b\\u0001\"", // JSON encoding
                                     },
                                     { NULL, NULL, NULL, NULL }
                                   };
	char buf[50] = {0};
        int i,j;

	style.binary=0;
	style.string_start = '"';
	style.string_end = '"';
	
	err = qio_file_open_tmp(&f, 0, NULL);
        assert(!err);
        
        for( i = 0; inputs[i][0]; i++ ) {
          const char* input = inputs[i][0];
          ssize_t input_len = strlen(input);
          for( j = 1; j < 4; j++ ) {
            const char* expect = inputs[i][j];
            ssize_t expect_len = strlen(expect);
            style.string_format = j;

            err = qio_channel_create(&writing, f, QIO_CH_BUFFERED, 0, 1, 0, INT64_MAX, &style);
            assert(!err);
            err = qio_channel_print_string(true, writing, input, input_len);	
            assert(!err);
            qio_channel_release(writing);

            err = qio_channel_create(&reading, f, QIO_CH_BUFFERED, 1, 0, 0, INT64_MAX, &style);
            assert(!err);
            err = qio_channel_read_amt(true, reading, buf, expect_len);
            assert(!err);
     
            qio_channel_release(reading);

            printf("Got %s expect %s\n", buf, expect);
            assert( memcmp(buf, expect, expect_len) == 0 );

            // Check that we can read it in again.
            err = qio_channel_create(&reading, f, QIO_CH_BUFFERED, 1, 0, 0, INT64_MAX, &style);
            assert(!err);

            {
              const char* got = NULL;
              ssize_t got_len = 0;
              err = qio_channel_scan_string(true, reading, &got, &got_len, -1);
              assert(!err);

              printf("Read back %s expect %s\n", got, input);
              assert( got_len == input_len );
              assert( memcmp(got, input, got_len) == 0 );

              free((void*) got);
            }
     
            qio_channel_release(reading);
          }
        }

        qio_file_release(f);

        printf("PASS: simple escape\n");
}

void write_65k_test()
{
        const char *out = NULL;
	char *p = NULL;
        err_t err;
        qio_file_t *f = NULL;
        qio_channel_t *reading = NULL;
        qio_channel_t *writing = NULL;
        qio_style_t style = qio_style_default();
	int buflen = 65535;
        ssize_t out_len = 0;

        err = qio_file_open_tmp(&f, 0, NULL);
        assert(!err);

        style.binary = 1;
        style.byteorder = QIO_BIG;
        style.str_style = -2;

        err = qio_channel_create(&writing, f, QIO_CH_BUFFERED, 0, 1, 0, INT64_MAX, &style);
        assert(!err);

	p = (char *)calloc(1, buflen);
	if(!p){ assert(0); }
	memset(p, 'A', buflen);

        err = qio_channel_write_string(true, style.byteorder, style.str_style, writing, p, buflen);
        assert(!err);
        qio_channel_release(writing);
        assert(!err);

        err = qio_channel_create(&reading, f, QIO_CH_BUFFERED, 1, 0, 0, INT64_MAX, NULL);
        assert(!err);

	while(1){
        	err = qio_channel_read_string(true, style.byteorder, style.str_style, reading, &out, &out_len, -1);
		if(out){
			if(memcmp(out, p, buflen)!=0){
				assert(0);
			}
			free((void*) out); out=NULL;
		}
		if(err>0){ break; }
	}

        qio_channel_release(reading);

	printf("PASS: wrote 65k to file, read it back ok.\n");

        qio_file_release(f);
        f = NULL;

	free(p);
}

/**
 *   
 **/
void max_width_test()
{
        const char *out = NULL;
        ssize_t out_len = 0;
        err_t err;
        qio_file_t *f = NULL;
        qio_channel_t *reading = NULL;
        qio_channel_t *writing = NULL;
        qio_style_t style = qio_style_default();

        err = qio_file_open_tmp(&f, 0, NULL);
        assert(!err);

        style.binary = 0;
        style.string_format = 1;
        style.max_width = 5;


        err = qio_channel_create(&writing, f, QIO_CH_BUFFERED, 0, 1, 0, INT64_MAX, &style);
        assert(!err);

        err = qio_channel_print_string(true, writing, "helloworld", 10);
        assert(!err);
        qio_channel_release(writing);
        assert(!err);

        err = qio_channel_create(&reading, f, QIO_CH_BUFFERED, 1, 0, 0, INT64_MAX, NULL);
        assert(!err);
        err = qio_channel_scan_string(true, reading, &out, &out_len, -1);
        assert(!err);
        qio_channel_release(reading);
        assert(!err);

        if(strlen(out) != 5){
                printf("FAIL: min_width expecting 11 chars for '%s'\n", out);
                assert(0);
        }

        free((void*) out);

        qio_file_release(f);
        f = NULL;
}

/**
 * Tests the min_width option.
 * by writing 10 byte helloworld string but specifying 
 * min_width = 11 so the read should return 11 bytes where
 * the 11th byte would be a space character.
 */
void min_width_test()
{
        const char *out = NULL;
        ssize_t out_len = 0;
        err_t err;
        qio_file_t *f = NULL;
        qio_channel_t *reading = NULL;
        qio_channel_t *writing = NULL;
	qio_style_t style = qio_style_default();

        err = qio_file_open_tmp(&f, 0, NULL);
        assert(!err);

	style.binary = 0;
	style.string_format = 1;
	style.min_width = 11;

        err = qio_channel_create(&writing, f, QIO_CH_BUFFERED, 0, 1, 0, INT64_MAX, &style);
        assert(!err);


        err = qio_channel_print_string(true, writing, "helloworld", 10);
        assert(!err);
        qio_channel_release(writing);

        err = qio_channel_create(&reading, f, QIO_CH_BUFFERED, 1, 0, 0, INT64_MAX, NULL);
        assert(!err);
        err = qio_channel_scan_string(true, reading, &out, &out_len, -1);
        assert(!err);
        qio_channel_release(reading);

	if(strlen(out) != 11){
		printf("FAIL: min_width expecting 11 chars for '%s'\n", out);
		assert(0);
	}

        free((void*) out);

        qio_file_release(f);
        f = NULL;
}

/**
 * Test basic ascii functionality
 */
void basicstring_test()
{ 
 	const char *out = NULL;
        ssize_t out_len = 0;
        err_t err;
	int x,y=0;
        qio_file_t *f = NULL;
        qio_channel_t *reading = NULL;
        qio_channel_t *writing = NULL;

	#define NUM_STR_STYLES 12
        qio_style_t styles[NUM_STR_STYLES];

	string_test_t strings[] = { 	
				    { "", 0 },
				    { "a", 1 },
				    { "AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA", 255},
				  }; 
        int NUM_STRINGS = sizeof(strings)/sizeof(string_test_t);

        printf("Testing basic string writing/reading\n");

        err = qio_file_open_tmp(&f, 0, NULL);
	assert(!err);

        for(x=0;x<NUM_STR_STYLES;x++) {
          qio_style_init_default(&styles[x]);
        }
        // Set up the styles
        // 0 is default.
        styles[0].binary = 1;
        styles[0].str_style = -1;

        // binary styles.
        styles[1].binary = 1;
        styles[1].str_style = -1;
        styles[2].binary = 1;
        styles[2].str_style = -2;
        styles[3].binary = 1;
        styles[3].str_style = -4;
        styles[4].binary = 1;
        styles[4].str_style = -8;
        styles[5].binary = 1;
        styles[5].str_style = -10;
        styles[6].binary = 1;
        styles[6].str_style = -0x0100;
        styles[7].binary = 1;
        styles[7].str_style = -0x01ff;
        styles[8].binary = 1;
        styles[8].str_style = 0xffff; // updated below for STYLE 8!

        styles[9].string_format = 1;
        styles[9].string_start = '|';
        styles[9].string_end = '+';
        styles[10].string_format = 2;
        styles[11].string_format = 3;


	for(x=0;x<NUM_STR_STYLES;x++){
          qio_style_t* style = &styles[x];
		for(y=0;y<NUM_STRINGS;y++){
                  string_test_t* string = &strings[y];

                        // make style 8 always use the string length!
                        if( x == 8 ) style->str_style = string->length;

        		err = qio_channel_create(&writing, f, QIO_CH_BUFFERED, 0, 1, 0, INT64_MAX, style);
			assert(!err);

                        printf("Writing string '%s' with style %i\n", string->string, x);

                        if( style->binary ) 
                          err = qio_channel_write_string(true, style->byteorder, style->str_style, writing, string->string, string->length);
                        else
                          err = qio_channel_print_string(true, writing, string->string, string->length);

			assert(!err);
        		qio_channel_release(writing);

			err = qio_channel_create(&reading, f, QIO_CH_BUFFERED, 1, 0, 0, INT64_MAX, style);
			assert(!err);

                        printf("Reading string '%s' with style %i\n", string->string, x);
                        if( style->binary ) 
                          err = qio_channel_read_string(true, style->byteorder, style->str_style, reading, &out, &out_len, -1);
                        else
                          err = qio_channel_scan_string(true, reading, &out, &out_len, -1);
			assert(!err);
			qio_channel_release(reading);

                        printf("Got '%s' expect '%s'\n", out, string->string);

			if(memcmp(out, string->string, string->length) != 0){
				printf("FAIL: style %d, string='%s'\n", x, string->string);
				free((void*) out);
				assert(0);
			}

			printf("PASS: style %d\n", x);
			if(out){ free((void*) out); out=NULL; }
		}
	}

        qio_file_release(f);
        f = NULL;
#undef NUM_STR_STYLES
#define NUM_STRINGS 1
}


void test_readwritestring()
{
	basicstring_test();
	write_65k_test();   //write 65k then read it back.

	//min_width_test();  //test min_width              
	//max_width_test();  //test max_width
	
        string_escape_tests();
}

void test_scanmatch()
{
  err_t err;
  qio_file_t *f = NULL;
  qio_channel_t *reading = NULL;
  qio_channel_t *writing = NULL;

  err = qio_file_open_tmp(&f, 0, NULL);
  assert(!err);

  err = qio_channel_create(&writing, f, QIO_CH_BUFFERED, 0, 1, 0, INT64_MAX, NULL);
  assert(!err);

  err = qio_channel_write_char(true, writing, ' ');
  assert(!err);
  err = qio_channel_write_char(true, writing, 'm');
  assert(!err);
  err = qio_channel_write_char(true, writing, 'a');
  assert(!err);
  err = qio_channel_write_char(true, writing, 't');
  assert(!err);
  err = qio_channel_write_char(true, writing, 'c');
  assert(!err);
  err = qio_channel_write_char(true, writing, 'h');
  assert(!err);


  qio_channel_release(writing);

  err = qio_channel_create(&reading, f, QIO_CH_BUFFERED, 1, 0, 0, INT64_MAX, NULL);
  assert(!err);

  err = qio_channel_scan_literal(true, reading, "test", 4, 1);
  assert(err == EFORMAT);
  err = qio_channel_scan_literal(true, reading, "match", 5, 1);
  assert(err == 0);
  err = qio_channel_scan_literal(true, reading, "match", 5, 1);
  assert(err == EEOF);

  qio_channel_release(reading);

  qio_file_release(f);
  f = NULL;
}

int main(int argc, char** argv)
{
  printf("Sizeof of qio_style_t is %i\n", (int) sizeof(qio_style_t));
  printf("Sizeof of qio_channel_t is %i\n", (int) sizeof(qio_channel_t));

  basicstring_test();

  test_verybasic();
  test_readwriteint();
  test_endian();
  test_printscan_int();
  test_printscan_float();

  test_readwritestring();

  test_scanmatch();

  return 0;
}

