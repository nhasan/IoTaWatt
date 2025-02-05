#include <RTC.h>

/**************************************************************************/
// utility code, some of this could be exposed in the DateTime API if needed
/**************************************************************************/

/**
  Number of days in each month, from January to November. December is not
  needed. Omitting it avoids an incompatibility with Paul Stoffregen's Time
  library. C.f. https://github.com/adafruit/RTClib/issues/114
*/
const uint8_t daysInMonth[] PROGMEM = {31, 28, 31, 30, 31, 30,
                                       31, 31, 30, 31, 30};

/**************************************************************************/
/*!
    @brief  Given a date, return number of days since 2000/01/01,
            valid for 2000--2099
    @param y Year
    @param m Month
    @param d Day
    @return Number of days
*/
/**************************************************************************/
static uint16_t date2days(uint16_t y, uint8_t m, uint8_t d) {
  if (y >= 2000U)
    y -= 2000U;
  uint16_t days = d;
  for (uint8_t i = 1; i < m; ++i)
    days += pgm_read_byte(daysInMonth + i - 1);
  if (m > 2 && y % 4 == 0)
    ++days;
  return days + 365 * y + (y + 3) / 4 - 1;
}

/**************************************************************************/
/*!
    @brief  Given a number of days, hours, minutes, and seconds, return the
   total seconds
    @param days Days
    @param h Hours
    @param m Minutes
    @param s Seconds
    @return Number of seconds total
*/
/**************************************************************************/
static uint32_t time2ulong(uint16_t days, uint8_t h, uint8_t m, uint8_t s) {
  return ((days * 24UL + h) * 60 + m) * 60 + s;
}

/**************************************************************************/
/*!
    @brief  Constructor from
        [Unix time](https://en.wikipedia.org/wiki/Unix_time).
    This builds a DateTime from an integer specifying the number of seconds
    elapsed since the epoch: 1970-01-01 00:00:00. This number is analogous
    to Unix time, with two small differences:
     - The Unix epoch is specified to be at 00:00:00
       [UTC](https://en.wikipedia.org/wiki/Coordinated_Universal_Time),
       whereas this class has no notion of time zones. The epoch used in
       this class is then at 00:00:00 on whatever time zone the user chooses
       to use, ignoring changes in DST.
     - Unix time is conventionally represented with signed numbers, whereas
       this constructor takes an unsigned argument. Because of this, it does
       _not_ suffer from the
       [year 2038 problem](https://en.wikipedia.org/wiki/Year_2038_problem).
    If called without argument, it returns the earliest time representable
    by this class: 2000-01-01 00:00:00.
    @see The `unixtime()` method is the converse of this constructor.
    @param t Time elapsed in seconds since 1970-01-01 00:00:00.
*/
/**************************************************************************/
DateTime::DateTime(uint32_t t) {
  t -= SECONDS_FROM_1970_TO_2000; // bring to 2000 timestamp from 1970

  ss = t % 60;
  t /= 60;
  mm = t % 60;
  t /= 60;
  hh = t % 24;
  uint16_t days = t / 24;
  uint8_t leap;
  for (yOff = 0;; ++yOff) {
    leap = yOff % 4 == 0;
    if (days < 365U + leap)
      break;
    days -= 365 + leap;
  }
  for (m = 1; m < 12; ++m) {
    uint8_t daysPerMonth = pgm_read_byte(daysInMonth + m - 1);
    if (leap && m == 2)
      ++daysPerMonth;
    if (days < daysPerMonth)
      break;
    days -= daysPerMonth;
  }
  d = days + 1;
}

/**************************************************************************/
/*!
    @brief  Constructor from (year, month, day, hour, minute, second).
    @warning If the provided parameters are not valid (e.g. 31 February),
           the constructed DateTime will be invalid.
    @see   The `isValid()` method can be used to test whether the
           constructed DateTime is valid.
    @param year Either the full year (range: 2000--2099) or the offset from
        year 2000 (range: 0--99).
    @param month Month number (1--12).
    @param day Day of the month (1--31).
    @param hour,min,sec Hour (0--23), minute (0--59) and second (0--59).
*/
/**************************************************************************/
DateTime::DateTime(uint16_t year, uint8_t month, uint8_t day, uint8_t hour,
                   uint8_t min, uint8_t sec) {
  if (year >= 2000U)
    year -= 2000U;
  yOff = year;
  m = month;
  d = day;
  hh = hour;
  mm = min;
  ss = sec;
}

/**************************************************************************/
/*!
    @brief  Copy constructor.
    @param copy DateTime to copy.
*/
/**************************************************************************/
DateTime::DateTime(const DateTime &copy)
    : yOff(copy.yOff), m(copy.m), d(copy.d), hh(copy.hh), mm(copy.mm),
      ss(copy.ss) {}

/**************************************************************************/
/*!
    @brief  Convert a string containing two digits to uint8_t, e.g. "09" returns
   9
    @param p Pointer to a string containing two digits
*/
/**************************************************************************/
static uint8_t conv2d(const char *p) {
  uint8_t v = 0;
  if ('0' <= p[0] && p[0] <= '9')
    v = p[0] - '0';
  if ('0' <= p[1] && p[1] <= '9')
    v = 10 * v + p[1] - '0';
  return v;
}

static uint8_t decodeMonth(const char *date) {
  int m = 0;
  switch (date[0]) {
  case 'J':
    m = (date[1] == 'a') ? 1 : ((date[2] == 'n') ? 6 : 7);
    break;
  case 'F':
    m = 2;
    break;
  case 'A':
    m = date[2] == 'r' ? 4 : 8;
    break;
  case 'M':
    m = date[2] == 'r' ? 3 : 5;
    break;
  case 'S':
    m = 9;
    break;
  case 'O':
    m = 10;
    break;
  case 'N':
    m = 11;
    break;
  case 'D':
    m = 12;
    break;
  }
  return m;
}

/**************************************************************************/
/*!
    @brief  Constructor for generating the build time.
    This constructor expects its parameters to be strings in the format
    generated by the compiler's preprocessor macros `__DATE__` and
    `__TIME__`. Usage:
    ```
    DateTime buildTime(__DATE__, __TIME__);
    ```
    @note The `F()` macro can be used to reduce the RAM footprint, see
        the next constructor.
    @param date Date string, e.g. "Apr 16 2020".
    @param time Time string, e.g. "18:34:56".
*/
/**************************************************************************/
DateTime::DateTime(const char *date, const char *time) {
  yOff = conv2d(date + 9);
  // Jan Feb Mar Apr May Jun Jul Aug Sep Oct Nov Dec
  m = decodeMonth(date);
  d = conv2d(date + 4);
  hh = conv2d(time);
  mm = conv2d(time + 3);
  ss = conv2d(time + 6);
}

/**************************************************************************/
/*!
    @brief  Memory friendly constructor for generating the build time.
    This version is intended to save RAM by keeping the date and time
    strings in program memory. Use it with the `F()` macro:
    ```
    DateTime buildTime(F(__DATE__), F(__TIME__));
    ```
    @param date Date PROGMEM string, e.g. F("Apr 16 2020").
    @param time Time PROGMEM string, e.g. F("18:34:56").
*/
/**************************************************************************/
DateTime::DateTime(const __FlashStringHelper *date,
                   const __FlashStringHelper *time) {
  char buff[11];
  memcpy_P(buff, date, 11);
  yOff = conv2d(buff + 9);
  // Jan Feb Mar Apr May Jun Jul Aug Sep Oct Nov Dec
  m = decodeMonth(buff);
  d = conv2d(buff + 4);
  memcpy_P(buff, time, 8);
  hh = conv2d(buff);
  mm = conv2d(buff + 3);
  ss = conv2d(buff + 6);
}

/**************************************************************************/
/*!
    @brief  Constructor for creating a DateTime from an ISO8601 date string.
    This constructor expects its parameters to be a string in the
    https://en.wikipedia.org/wiki/ISO_8601 format, e.g:
    "2020-06-25T15:29:37"
    Usage:
    ```
    DateTime dt("2020-06-25T15:29:37");
    ```
    @note The year must be > 2000, as only the yOff is considered.
    @param iso8601dateTime
           A dateTime string in iso8601 format,
           e.g. "2020-06-25T15:29:37".
*/
/**************************************************************************/
DateTime::DateTime(const char *iso8601dateTime) {
  char ref[] = "2000-01-01T00:00:00";
  memcpy(ref, iso8601dateTime, min(strlen(ref), strlen(iso8601dateTime)));
  yOff = conv2d(ref + 2);
  m = conv2d(ref + 5);
  d = conv2d(ref + 8);
  hh = conv2d(ref + 11);
  mm = conv2d(ref + 14);
  ss = conv2d(ref + 17);
}

/**************************************************************************/
/*!
    @brief  Check whether this DateTime is valid.
    @return true if valid, false if not.
*/
/**************************************************************************/
bool DateTime::isValid() const {
  if (yOff >= 100)
    return false;
  DateTime other(unixtime());
  return yOff == other.yOff && m == other.m && d == other.d && hh == other.hh &&
         mm == other.mm && ss == other.ss;
}

/**************************************************************************/
/*!
    @brief  Writes the DateTime as a string in a user-defined format.
    The _buffer_ parameter should be initialized by the caller with a string
    specifying the requested format. This format string may contain any of
    the following specifiers:
    | specifier | output                                                 |
    |-----------|--------------------------------------------------------|
    | YYYY      | the year as a 4-digit number (2000--2099)              |
    | YY        | the year as a 2-digit number (00--99)                  |
    | MM        | the month as a 2-digit number (01--12)                 |
    | MMM       | the abbreviated English month name ("Jan"--"Dec")      |
    | DD        | the day as a 2-digit number (01--31)                   |
    | DDD       | the abbreviated English day of the week ("Mon"--"Sun") |
    | AP        | either "AM" or "PM"                                    |
    | ap        | either "am" or "pm"                                    |
    | hh        | the hour as a 2-digit number (00--23 or 01--12)        |
    | mm        | the minute as a 2-digit number (00--59)                |
    | ss        | the second as a 2-digit number (00--59)                |
    If either "AP" or "ap" is used, the "hh" specifier uses 12-hour mode
    (range: 01--12). Otherwise it works in 24-hour mode (range: 00--23).
    The specifiers within _buffer_ will be overwritten with the appropriate
    values from the DateTime. Any characters not belonging to one of the
    above specifiers are left as-is.
    __Example__: The format "DDD, DD MMM YYYY hh:mm:ss" generates an output
    of the form "Thu, 16 Apr 2020 18:34:56.
    @see The `timestamp()` method provides similar functionnality, but it
        returns a `String` object and supports a limited choice of
        predefined formats.
    @param[in,out] buffer Array of `char` for holding the format description
        and the formatted DateTime. Before calling this method, the buffer
        should be initialized by the user with the format string. The method
        will overwrite the buffer with the formatted date and/or time.
    @return A pointer to the provided buffer. This is returned for
        convenience, in order to enable idioms such as
        `Serial.println(now.toString(buffer));`
*/
/**************************************************************************/

char *DateTime::toString(char *buffer) {
  uint8_t apTag =
      (strstr(buffer, "ap") != nullptr) || (strstr(buffer, "AP") != nullptr);
  uint8_t hourReformatted = hh, isPM = false;
  if (apTag) {     // 12 Hour Mode
    if (hh == 0) { // midnight
      hourReformatted = 12;
    } else if (hh == 12) { // noon
      isPM = true;
    } else if (hh > 12) { // morning
      isPM = true;
      hourReformatted -= 12;
    }
  }

  for (size_t i = 0; i < strlen(buffer) - 1; i++) {
    if (buffer[i] == 'h' && buffer[i + 1] == 'h') {
      buffer[i] = '0' + hourReformatted / 10;
      buffer[i + 1] = '0' + hourReformatted % 10;
    }
    if (buffer[i] == 'm' && buffer[i + 1] == 'm') {
      buffer[i] = '0' + mm / 10;
      buffer[i + 1] = '0' + mm % 10;
    }
    if (buffer[i] == 's' && buffer[i + 1] == 's') {
      buffer[i] = '0' + ss / 10;
      buffer[i + 1] = '0' + ss % 10;
    }
    if (buffer[i] == 'D' && buffer[i + 1] == 'D') {
      if (buffer[i + 2] == 'D') {
        static PROGMEM const char day_names[] = "SunMonTueWedThuFriSat";
        const char *p = &day_names[3 * dayOfTheWeek()];
        buffer[i] = pgm_read_byte(p);
        buffer[i + 1] = pgm_read_byte(p + 1);
        buffer[i + 2] = pgm_read_byte(p + 2);
      } else {
        buffer[i] = '0' + d / 10;
        buffer[i + 1] = '0' + d % 10;
      }
    }
    if (buffer[i] == 'M' && buffer[i + 1] == 'M') {
      if (buffer[i + 2] == 'M') {
        static PROGMEM const char month_names[] =
            "JanFebMarAprMayJunJulAugSepOctNovDec";
        const char *p = &month_names[3 * (m - 1)];
        buffer[i] = pgm_read_byte(p);
        buffer[i + 1] = pgm_read_byte(p + 1);
        buffer[i + 2] = pgm_read_byte(p + 2);
      } else {
        buffer[i] = '0' + m / 10;
        buffer[i + 1] = '0' + m % 10;
      }
    }
    if (buffer[i] == 'Y' && buffer[i + 1] == 'Y') {
      if (buffer[i + 2] == 'Y' && buffer[i + 3] == 'Y') {
        buffer[i] = '2';
        buffer[i + 1] = '0';
        buffer[i + 2] = '0' + (yOff / 10) % 10;
        buffer[i + 3] = '0' + yOff % 10;
      } else {
        buffer[i] = '0' + (yOff / 10) % 10;
        buffer[i + 1] = '0' + yOff % 10;
      }
    }
    if (buffer[i] == 'A' && buffer[i + 1] == 'P') {
      if (isPM) {
        buffer[i] = 'P';
        buffer[i + 1] = 'M';
      } else {
        buffer[i] = 'A';
        buffer[i + 1] = 'M';
      }
    } else if (buffer[i] == 'a' && buffer[i + 1] == 'p') {
      if (isPM) {
        buffer[i] = 'p';
        buffer[i + 1] = 'm';
      } else {
        buffer[i] = 'a';
        buffer[i + 1] = 'm';
      }
    }
  }
  return buffer;
}

/**************************************************************************/
/*!
      @brief  Return the hour in 12-hour format.
      @return Hour (1--12).
*/
/**************************************************************************/
uint8_t DateTime::twelveHour() const {
  if (hh == 0 || hh == 12) { // midnight or noon
    return 12;
  } else if (hh > 12) { // 1 o'clock or later
    return hh - 12;
  } else { // morning
    return hh;
  }
}

/**************************************************************************/
/*!
    @brief  Return the day of the week.
    @return Day of week as an integer from 0 (Sunday) to 6 (Saturday).
*/
/**************************************************************************/
uint8_t DateTime::dayOfTheWeek() const {
  uint16_t day = date2days(yOff, m, d);
  return (day + 6) % 7; // Jan 1, 2000 is a Saturday, i.e. returns 6
}

/**************************************************************************/
/*!
    @brief  Return Unix time: seconds since 1 Jan 1970.
    @see The `DateTime::DateTime(uint32_t)` constructor is the converse of
        this method.
    @return Number of seconds since 1970-01-01 00:00:00.
*/
/**************************************************************************/
uint32_t DateTime::unixtime(void) const {
  uint32_t t;
  uint16_t days = date2days(yOff, m, d);
  t = time2ulong(days, hh, mm, ss);
  t += SECONDS_FROM_1970_TO_2000; // seconds from 1970 to 2000

  return t;
}

/**************************************************************************/
/*!
    @brief  Convert the DateTime to seconds since 1 Jan 2000
    The result can be converted back to a DateTime with:
    ```cpp
    DateTime(SECONDS_FROM_1970_TO_2000 + value)
    ```
    @return Number of seconds since 2000-01-01 00:00:00.
*/
/**************************************************************************/
uint32_t DateTime::secondstime(void) const {
  uint32_t t;
  uint16_t days = date2days(yOff, m, d);
  t = time2ulong(days, hh, mm, ss);
  return t;
}

/**************************************************************************/
/*!
    @brief  Add a TimeSpan to the DateTime object
    @param span TimeSpan object
    @return New DateTime object with span added to it.
*/
/**************************************************************************/
DateTime DateTime::operator+(const TimeSpan &span) {
  return DateTime(unixtime() + span.totalseconds());
}

/**************************************************************************/
/*!
    @brief  Subtract a TimeSpan from the DateTime object
    @param span TimeSpan object
    @return New DateTime object with span subtracted from it.
*/
/**************************************************************************/
DateTime DateTime::operator-(const TimeSpan &span) {
  return DateTime(unixtime() - span.totalseconds());
}

/**************************************************************************/
/*!
    @brief  Subtract one DateTime from another
    @note Since a TimeSpan cannot be negative, the subtracted DateTime
        should be less (earlier) than or equal to the one it is
        subtracted from.
    @param right The DateTime object to subtract from self (the left object)
    @return TimeSpan of the difference between DateTimes.
*/
/**************************************************************************/
TimeSpan DateTime::operator-(const DateTime &right) {
  return TimeSpan(unixtime() - right.unixtime());
}

/**************************************************************************/
/*!
    @author Anton Rieutskyi
    @brief  Test if one DateTime is less (earlier) than another.
    @warning if one or both DateTime objects are invalid, returned value is
        meaningless
    @see use `isValid()` method to check if DateTime object is valid
    @param right Comparison DateTime object
    @return True if the left DateTime is earlier than the right one,
        false otherwise.
*/
/**************************************************************************/
bool DateTime::operator<(const DateTime &right) const {
  return (yOff + 2000U < right.year() ||
          (yOff + 2000U == right.year() &&
           (m < right.month() ||
            (m == right.month() &&
             (d < right.day() ||
              (d == right.day() &&
               (hh < right.hour() ||
                (hh == right.hour() &&
                 (mm < right.minute() ||
                  (mm == right.minute() && ss < right.second()))))))))));
}

/**************************************************************************/
/*!
    @author Anton Rieutskyi
    @brief  Test if two DateTime objects are equal.
    @warning if one or both DateTime objects are invalid, returned value is
        meaningless
    @see use `isValid()` method to check if DateTime object is valid
    @param right Comparison DateTime object
    @return True if both DateTime objects are the same, false otherwise.
*/
/**************************************************************************/
bool DateTime::operator==(const DateTime &right) const {
  return (right.year() == yOff + 2000U && right.month() == m &&
          right.day() == d && right.hour() == hh && right.minute() == mm &&
          right.second() == ss);
}

/**************************************************************************/
/*!
    @brief  Return a ISO 8601 timestamp as a `String` object.
    The generated timestamp conforms to one of the predefined, ISO
    8601-compatible formats for representing the date (if _opt_ is
    `TIMESTAMP_DATE`), the time (`TIMESTAMP_TIME`), or both
    (`TIMESTAMP_FULL`).
    @see The `toString()` method provides more general string formatting.
    @param opt Format of the timestamp
    @return Timestamp string, e.g. "2020-04-16T18:34:56".
*/
/**************************************************************************/
String DateTime::timestamp(timestampOpt opt) {
  char buffer[25]; // large enough for any DateTime, including invalid ones

  // Generate timestamp according to opt
  switch (opt) {
  case TIMESTAMP_TIME:
    // Only time
    sprintf(buffer, "%02d:%02d:%02d", hh, mm, ss);
    break;
  case TIMESTAMP_DATE:
    // Only date
    sprintf(buffer, "%u-%02d-%02d", 2000U + yOff, m, d);
    break;
  default:
    // Full
    sprintf(buffer, "%u-%02d-%02dT%02d:%02d:%02d", 2000U + yOff, m, d, hh, mm,
            ss);
  }
  return String(buffer);
}

/**************************************************************************/
/*!
    @brief  Create a new TimeSpan object in seconds
    @param seconds Number of seconds
*/
/**************************************************************************/
TimeSpan::TimeSpan(int32_t seconds) : _seconds(seconds) {}

/**************************************************************************/
/*!
    @brief  Create a new TimeSpan object using a number of
   days/hours/minutes/seconds e.g. Make a TimeSpan of 3 hours and 45 minutes:
   new TimeSpan(0, 3, 45, 0);
    @param days Number of days
    @param hours Number of hours
    @param minutes Number of minutes
    @param seconds Number of seconds
*/
/**************************************************************************/
TimeSpan::TimeSpan(int16_t days, int8_t hours, int8_t minutes, int8_t seconds)
    : _seconds((int32_t)days * 86400L + (int32_t)hours * 3600 +
               (int32_t)minutes * 60 + seconds) {}

/**************************************************************************/
/*!
    @brief  Copy constructor, make a new TimeSpan using an existing one
    @param copy The TimeSpan to copy
*/
/**************************************************************************/
TimeSpan::TimeSpan(const TimeSpan &copy) : _seconds(copy._seconds) {}

/**************************************************************************/
/*!
    @brief  Add two TimeSpans
    @param right TimeSpan to add
    @return New TimeSpan object, sum of left and right
*/
/**************************************************************************/
TimeSpan TimeSpan::operator+(const TimeSpan &right) {
  return TimeSpan(_seconds + right._seconds);
}

/**************************************************************************/
/*!
    @brief  Subtract a TimeSpan
    @param right TimeSpan to subtract
    @return New TimeSpan object, right subtracted from left
*/
/**************************************************************************/
TimeSpan TimeSpan::operator-(const TimeSpan &right) {
  return TimeSpan(_seconds - right._seconds);
}

/**************************************************************************/
/*!
    @brief  Convert a binary coded decimal value to binary. RTC stores time/date
   values as BCD.
    @param val BCD value
    @return Binary value
*/
/**************************************************************************/
static uint8_t bcd2bin(uint8_t val) { return val - 6 * (val >> 4); }

/**************************************************************************/
/*!
    @brief  Convert a binary value to BCD format for the RTC registers
    @param val Binary value
    @return BCD value
*/
/**************************************************************************/
static uint8_t bin2bcd(uint8_t val) { return val + 6 * (val / 10); }

/**************************************************************************/
/*!
    @brief  Read a byte from the RTC
    @param deviceAddress I2C address of the device
    @param memoryAddress Device memory address to begin reading
    @param len number of bytes to read
    @return the byte
*/
/**************************************************************************/
void RTC::readBytes(uint8_t memoryAddress, uint8_t len){
    RTCWireBus->beginTransmission(_RTCaddr);
    RTCWireBus->write(memoryAddress);
    RTCWireBus->endTransmission();
    RTCWireBus->requestFrom(_RTCaddr, len);
    return;
}
        
void RTC::dumpRegs(TwoWire *wireInstance){
    RTCWireBus = wireInstance;
    _model = M41T81;
    _RTCaddr = PCF8523_ADDR;
    readBytes((byte) 0, 20);
    Serial.print("RTC: ");
    for (uint8_t i = 1; i < 21; i++){
        uint8_t ss = RTCWireBus->read();
        Serial.printf("%02X", ss);
        if( !(i % 4))
            Serial.print(' ');
    }
    Serial.println();
}

boolean RTC::begin(TwoWire *wireInstance){
    RTCWireBus = wireInstance;
    
    // Here is where the clock model must be identified.

    RTCWireBus->beginTransmission(M41T81_ADDR);
    if (RTCWireBus->endTransmission() == 0){
        _model = M41T81;
        _RTCaddr = M41T81_ADDR;
        return true;
    }

    RTCWireBus->beginTransmission(PCF8523_ADDR);
    if (RTCWireBus->endTransmission() == 0){
        _RTCaddr = PCF8523_ADDR;

        // The M41T81 data-sheet specifies an I2C address of 0xD0,
        // but some of them see tom use 0x68.   That's the same as
        // the PCF8523, so we need to disambiguate.
        // Write 0xff to register 0x10

        RTCWireBus->beginTransmission(_RTCaddr);
        RTCWireBus->write((byte)0x10);
        RTCWireBus->write((byte)0xff);
        RTCWireBus->endTransmission();

        // Now read it back.
        // PCF8523 will send back 0x07
        // M41T81 will send back 0x00

        readBytes((byte) 0x10);
        uint8_t byte10 = RTCWireBus->read();
        if(byte10 == 0x07){
            _model = PCF8523;
        }
        else {
            _model = M41T81;
        }
        return true;
    }
    _model = unknown;
    return false;
}

boolean RTC::isRunning(){
    if(_model == unknown){
        return false;
    }
    if(_model == PCF8523){
        readBytes((byte)PCF8523_CONTROL_3);
        uint8_t ss = RTCWireBus->read();
        return ((ss & 0xE0) != 0xE0);       // 0xE0 = standby mode, set after power out
    }
    if(_model == M41T81){
        readBytes((byte) 0x01);
        return  ! ((RTCWireBus->read() & 0x80) >> 7);
    }
    return false;
}

boolean RTC::lostPower(void){
    if(_model == unknown){
        return false;
    }
    if(_model == PCF8523){
        readBytes(PCF8523_CONTROL_3);
        return (RTCWireBus->read() & 0x08) >> 3; // BSF (Battery Switchover Flag) set
    }
    if(_model == M41T81){
        readBytes((byte) 12);
        return (RTCWireBus->read() & 0x40) >> 6;  // HT (Halt Update bit) set
    }
    return false;
}

void RTC::resetLostPower(){
    RTCWireBus->beginTransmission(_RTCaddr);
    if(_model == PCF8523){
        RTCWireBus->write((byte)PCF8523_CONTROL_3);
    }
    else if(_model == M41T81){
        RTCWireBus->write((byte)0x0C); // HT byte
    }
    RTCWireBus->write((byte)0);
    RTCWireBus->endTransmission();
}

void RTC::adjust(const DateTime &dt){
    if(_model == unknown){
        return;
    }
    RTCWireBus->beginTransmission(_RTCaddr);
    if(_model == PCF8523){
        RTCWireBus->write((byte)PCF8523_CONTROL_3);
        RTCWireBus->write(0x00); 
        RTCWireBus->write(bin2bcd(dt.second()));    // 02h - Set battery switchover mode
        RTCWireBus->write(bin2bcd(dt.minute()));
        RTCWireBus->write(bin2bcd(dt.hour()));
        RTCWireBus->write(bin2bcd(dt.day()));
        RTCWireBus->write(bin2bcd(0)); // skip weekdays
        RTCWireBus->write(bin2bcd(dt.month()));
        RTCWireBus->write(bin2bcd(dt.year() - 2000U));
    }
    else if(_model == M41T81){
        RTCWireBus->write((byte)0x01);              
        RTCWireBus->write(bin2bcd(dt.second()));    // 01h - second
        RTCWireBus->write(bin2bcd(dt.minute()));    // 02h - minute
        RTCWireBus->write(bin2bcd(dt.hour()));      // 03h - hour
        RTCWireBus->write((byte)0x00);              // 04h - day in week (set to 0)
        RTCWireBus->write(bin2bcd(dt.day()));       // 05h - day in month
        RTCWireBus->write(bin2bcd(dt.month()));     // 06h - month
        RTCWireBus->write(bin2bcd(dt.year() - 2000U)); // 07h - year in century
        RTCWireBus->write((byte)0x80);              // 08h - OUT=1, FT=0, S=0, Calibration = 0
        RTCWireBus->write((byte)0x80);              // 09h - D7=1, BMBn=0, RBn=0 
        RTCWireBus->write((byte)0x00);              // 0Ah - AFE=0, SQWE=0, ABE=0, ALM=0
        RTCWireBus->write((byte)0x00);              // 0Bh - 0
        RTCWireBus->write((byte)0x00);              // 0Ch - HT=0
        RTCWireBus->write((byte)0x00);              // 0Dh - AFE=0, SQWE=0, ABE=1, ALM=0
        RTCWireBus->write((byte)0x00);              // 0Eh - AFE=0, SQWE=0, ABE=1, ALM=0
        RTCWireBus->write((byte)0x00);              // 0Fh - AFE=0, SQWE=0, ABE=1, ALM=0
    }
    RTCWireBus->endTransmission();
}

DateTime RTC::now(){
    if(_model == PCF8523){
        readBytes((byte) 0x03, (byte) 7);
        uint8_t ss = bcd2bin(RTCWireBus->read() & 0x7F);
        uint8_t mm = bcd2bin(RTCWireBus->read() & 0x7F);
        uint8_t hh = bcd2bin(RTCWireBus->read() & 0x3F);
        uint8_t d = bcd2bin(RTCWireBus->read() & 0x3F);
        RTCWireBus->read(); // skip 'weekdays'
        uint8_t m = bcd2bin(RTCWireBus->read() & 0x1F);
        uint16_t y = bcd2bin(RTCWireBus->read()) + 2000U;
        return DateTime(y, m, d, hh, mm, ss);
    }
    else if(_model == M41T81){
        readBytes((byte) 0x01, (byte) 7);
        uint8_t ss = bcd2bin(RTCWireBus->read() & 0x7F);
        uint8_t mm = bcd2bin(RTCWireBus->read() & 0x7F);
        uint8_t hh = bcd2bin(RTCWireBus->read() & 0x3F);
        RTCWireBus->read(); // skip 'weekdays'
        uint8_t d = bcd2bin(RTCWireBus->read() & 0x3F);
        uint8_t m = bcd2bin(RTCWireBus->read() & 0x1F);
        uint16_t y = bcd2bin(RTCWireBus->read()) + 2000U;
        return DateTime(y, m, d, hh, mm, ss);
    }
    else {
        return DateTime(0, 1, 1, 0, 0, 0);
    }
}

boolean RTC::lowBattery(){
    if(_model == PCF8523){
        readBytes((byte) PCF8523_CONTROL_3);
        return  (RTCWireBus->read() & 0x04) >> 2;
    }
    if(_model == M41T81){
        readBytes((byte) 0x0f);
        return  (RTCWireBus->read() & 0x01) >>4;
    }
    return false;
}

void RTC::stop(){
    RTCWireBus->beginTransmission(_RTCaddr);
    if(_model == PCF8523){
        RTCWireBus->write((byte)PCF8523_CONTROL_3);
        RTCWireBus->write((byte)0xE0);
    }
    else if(_model == M41T81){
        RTCWireBus->write((byte)0x01); // HT byte
        RTCWireBus->write((byte)0x80);
    }
    RTCWireBus->endTransmission();
}

String RTC::model(){
  char response[32] = "unknown";
  if (_model == PCF8523){
    sprintf(response, "PCF8523 (%.2x)", _RTCaddr);
  }
  if(_model == M41T81){
      sprintf(response, "M41T81 (%.2x)", _RTCaddr);
  }
  return String(response);
}

boolean RTC::isPCF8525(){
    return _model == PCF8523;
}

boolean RTC::isM41T81(){
    return _model == M41T81;
}
