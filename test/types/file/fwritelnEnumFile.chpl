enum precipitation {mist, sprinkle, drizzle, rain, shower};
var todaysWeather: precipitation = precipitation.sprinkle;

var f = open("_test_fwritelnEnumFile.txt", "w").writer();

f.writeln(todaysWeather);

f.close();
