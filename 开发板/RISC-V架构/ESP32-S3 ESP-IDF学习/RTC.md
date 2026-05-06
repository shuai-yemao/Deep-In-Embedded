### RTC简介
RTC（实时时钟）是指安装在电子设备或实现其功能的 IC（集成电路）上的时钟
通常，RTC 配备一个单独分离的电源，如纽扣电池（备用电池）。

### RTC函数解析
struct tm *localtime(const time_t *timer);
	该函数用于获取当前时间
	参数表
		timer 这是指向表示日历时间的 time_t 值的指针
int settimeofday(const struct timeval *tv, const struct timezone *tz);
	该函数用于设置当前时间
	参数表
		![[Pasted image 20251007162548.png]]
	
$$
/* 时间结构体, 包括年月日周时分秒等信息 */
typedef struct
{
 uint8_t hour; /* 时 */
 uint8_t min; /* 分 */
 uint8_t sec; /* 秒 */
 /* 公历年月日周 */
 uint16_t year; /* 年 */
 uint8_t month; /* 月 */
 uint8_t date; /* 日 */
 uint8_t week; /* 周 */
} _calendar_obj;
extern _calendar_obj calendar; /* 时间结构体 */

calendar_obj calendar; /* 时间结构体 */
/**
* @brief RTC 设置时间
* @param year :年
* @param mon :月
* @param mday :日
* @param hour :时
* @param min :分
* @param sec :秒
* @retval 无
*/
void rtc_set_time(int year,int mon,int mday,int hour,int min,int sec)
{
 struct tm datetime;
 /* 设置时间 */
 datetime.tm_year = year - 1900;
 datetime.tm_mon = mon - 1;
 datetime.tm_mday = mday;
 datetime.tm_hour = hour;
 datetime.tm_min = min;
 datetime.tm_sec = sec;
 datetime.tm_isdst = -1;
 /* 获取 1970.1.1 以来的总秒数 */
 time_t second = mktime(&datetime);
 struct timeval val = { .tv_sec = second, .tv_usec = 0 };
 /* 设置当前时间 */
 settimeofday(&val, NULL);
}

/**
* @brief 获取当前的时间
* @param 无
* @retval 无
*/
void rtc_get_time(void)
{
 struct tm *datetime;
 time_t second;
 /* 返回自(1970.1.1 00:00:00 UTC)经过的时间(秒) */
 time(&second);
 datetime = localtime(&second);
 calendar.hour = datetime->tm_hour; /* 时 */
 calendar.min = datetime->tm_min; /* 分 */
 calendar.sec = datetime->tm_sec; /* 秒 */
 /* 公历年月日周 */
 calendar.year = datetime->tm_year + 1900; /* 年 */
 calendar.month = datetime->tm_mon + 1; /* 月 */
 calendar.date = datetime->tm_mday; /* 日 */
 /* 周 */
 calendar.week = rtc_get_week(calendar.year, calendar.month, calendar.date);
}

/**
* @brief 将年月日时分秒转换成秒钟数
* @note 输入公历日期得到星期(起始时间为: 公元 0 年 3 月 1 日开始, 输入往后的任何日期,都
 可以获取正确的星期)
* 使用 基姆拉尔森计算公式 计算, 原理说明见此贴:
* https://www.cnblogs.com/fengbohello/p/3264300.html
* @param syear : 年份
* @param smon : 月份
* @param sday : 日期
* @retval 0, 星期天; 1 ~ 6: 星期一 ~ 星期六
*/
uint8_t rtc_get_week(uint16_t year, uint8_t month, uint8_t day)
{
 uint8_t week = 0;
 if (month < 3)
 {
 month += 12;
 --year;
 }
week = (day + 1 + 2 * month + 3 * (month + 1) /
5 + year + (year >> 2) - year /
100 + year / 400) % 7;
 return week;
}

$$