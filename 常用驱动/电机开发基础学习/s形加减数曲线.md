# STM32 步进电机梯形曲线与 S 形加减速学习笔记

> uTools Markdown 适配版  
控制对象：STM32 + EBF-MSD4805 + 两相步进电机  
主题：STEP/DIR 控制、梯形速度曲线、S 形速度曲线、jerk、定时器 ARR

## 一、STM32 实际控制对象

STM32 通常不直接向步进电机绕组输出 SPWM 或 SVPWM，而是向外部步进驱动器输出：

|信号|作用|
|--|--|
|<code>PUL/STEP</code>|每个有效边沿推进一个整步或微步|
|<code>DIR</code>|设置转动方向|
|<code>ENA</code>|使能或禁止驱动器|

驱动器内部负责绕组电流调节和细分。STM32 主要通过改变 STEP 脉冲时间间隔来控制速度。

$$
\text{STEP 频率越高}\Rightarrow\text{电机转速越高}
$$

$$
\text{STEP 频率越低}\Rightarrow\text{电机转速越低}
$$

## 二、梯形曲线与 S 形曲线总览

<img alt="STM32 步进电机梯形曲线与 S 形加减速总览图" src="data:image/svg+xml;base64,PHN2ZyB4bWxucz0iaHR0cDovL3d3dy53My5vcmcvMjAwMC9zdmciIHdpZHRoPSIxNjAwIiBoZWlnaHQ9IjI4NTAiIHZpZXdCb3g9IjAgMCAxNjAwIDI4NTAiIHJvbGU9ImltZyIgYXJpYS1sYWJlbGxlZGJ5PSJ0aXRsZSBkZXNjIj4KICA8dGl0bGUgaWQ9InRpdGxlIj5TVE0zMiDmraXov5vnlLXmnLrmoq/lvaLmm7Lnur/kuI4gUyDlvaLliqDlh4/pgJ/mm7Lnur/lr7nnhac8L3RpdGxlPgogIDxkZXNjIGlkPSJkZXNjIj7mr5TovoPmoq/lvaLpgJ/luqbmm7Lnur/lkowgUyDlvaLpgJ/luqbmm7Lnur/vvIzlsZXnpLogamVya+OAgeWKoOmAn+W6puOAgVNURVAg6aKR546H44CB5LiD5q616L+Q5Yqo6L+H56iL5Lul5Y+KIFNUTTMyIOWumuaXtuWZqCBBUlIg6L2s5o2i6ZO+44CCPC9kZXNjPgogIDxkZWZzPgogICAgPGxpbmVhckdyYWRpZW50IGlkPSJoZWFkZXIiIHgxPSIwIiB4Mj0iMSIgeTE9IjAiIHkyPSIxIj4KICAgICAgPHN0b3Agb2Zmc2V0PSIwIiBzdG9wLWNvbG9yPSIjMTIzYjY2Ii8+CiAgICAgIDxzdG9wIG9mZnNldD0iMSIgc3RvcC1jb2xvcj0iIzFiNmE3OCIvPgogICAgPC9saW5lYXJHcmFkaWVudD4KICAgIDxsaW5lYXJHcmFkaWVudCBpZD0ic2N1cnZlIiB4MT0iMCIgeDI9IjEiPgogICAgICA8c3RvcCBvZmZzZXQ9IjAiIHN0b3AtY29sb3I9IiM2ZTljZTgiLz4KICAgICAgPHN0b3Agb2Zmc2V0PSIxIiBzdG9wLWNvbG9yPSIjMmY5Yjc4Ii8+CiAgICA8L2xpbmVhckdyYWRpZW50PgogICAgPHN0eWxlPgogICAgICAuYmd7ZmlsbDojZjVmOGZifQogICAgICAudGl0bGV7Zm9udC1mYW1pbHk6Ik1pY3Jvc29mdCBZYUhlaSIsIk5vdG8gU2FucyBDSksgU0MiLHNhbnMtc2VyaWY7Zm9udC1zaXplOjQycHg7Zm9udC13ZWlnaHQ6NzAwO2ZpbGw6I2Y3ZmJmZn0KICAgICAgLnN1YnRpdGxle2ZvbnQtZmFtaWx5OiJNaWNyb3NvZnQgWWFIZWkiLCJOb3RvIFNhbnMgQ0pLIFNDIixzYW5zLXNlcmlmO2ZvbnQtc2l6ZToyMnB4O2ZpbGw6I2Q4ZWRmNX0KICAgICAgLmh7Zm9udC1mYW1pbHk6Ik1pY3Jvc29mdCBZYUhlaSIsIk5vdG8gU2FucyBDSksgU0MiLHNhbnMtc2VyaWY7Zm9udC1zaXplOjI5cHg7Zm9udC13ZWlnaHQ6NzAwO2ZpbGw6IzEyM2I2Nn0KICAgICAgLmJvZHl7Zm9udC1mYW1pbHk6Ik1pY3Jvc29mdCBZYUhlaSIsIk5vdG8gU2FucyBDSksgU0MiLHNhbnMtc2VyaWY7Zm9udC1zaXplOjIxcHg7ZmlsbDojMjYzNzQ2fQogICAgICAuc21hbGx7Zm9udC1mYW1pbHk6Ik1pY3Jvc29mdCBZYUhlaSIsIk5vdG8gU2FucyBDSksgU0MiLHNhbnMtc2VyaWY7Zm9udC1zaXplOjE4cHg7ZmlsbDojNTI2Nzc5fQogICAgICAuZm9ybXVsYXtmb250LWZhbWlseTpDb25zb2xhcywiTm90byBTYW5zIE1vbm8iLG1vbm9zcGFjZTtmb250LXNpemU6MjVweDtmaWxsOiMxMDJhNDN9CiAgICAgIC5ib3h7ZmlsbDojZmZmZmZmO3N0cm9rZTojYzdkNmUyO3N0cm9rZS13aWR0aDoyfQogICAgICAuYmFuZHtmaWxsOiNlYWYyZjg7c3Ryb2tlOiNiOWNmZGU7c3Ryb2tlLXdpZHRoOjJ9CiAgICAgIC53YXJue2ZpbGw6I2ZmZjVlNztzdHJva2U6I2U0YmI4NDtzdHJva2Utd2lkdGg6Mn0KICAgICAgLm9re2ZpbGw6I2VkZjhmMztzdHJva2U6IzljY2RiODtzdHJva2Utd2lkdGg6Mn0KICAgICAgLmF4aXN7c3Ryb2tlOiM1ZDcxODE7c3Ryb2tlLXdpZHRoOjJ9CiAgICAgIC5ncmlke3N0cm9rZTojZDZlMWU4O3N0cm9rZS13aWR0aDoxfQogICAgICAudHJhcHtzdHJva2U6I2QzNmI0YztzdHJva2Utd2lkdGg6NztmaWxsOm5vbmV9CiAgICAgIC5zbW9vdGh7c3Ryb2tlOnVybCgjc2N1cnZlKTtzdHJva2Utd2lkdGg6NztmaWxsOm5vbmV9CiAgICAgIC5qZXJre3N0cm9rZTojODQ1OWI3O3N0cm9rZS13aWR0aDo1O2ZpbGw6bm9uZX0KICAgICAgLmFjY2Vse3N0cm9rZTojZDg5MTJlO3N0cm9rZS13aWR0aDo1O2ZpbGw6bm9uZX0KICAgICAgLmxhYmVse2ZvbnQtZmFtaWx5OiJNaWNyb3NvZnQgWWFIZWkiLCJOb3RvIFNhbnMgQ0pLIFNDIixzYW5zLXNlcmlmO2ZvbnQtc2l6ZToxOXB4O2ZvbnQtd2VpZ2h0OjcwMDtmaWxsOiMyNDQ4NWZ9CiAgICAgIC5wbG90bGFiZWx7Zm9udC1mYW1pbHk6Ik1pY3Jvc29mdCBZYUhlaSIsIk5vdG8gU2FucyBDSksgU0MiLHNhbnMtc2VyaWY7Zm9udC1zaXplOjE3cHg7ZmlsbDojM2U1NjY4fQogICAgICAubW9ub1NtYWxse2ZvbnQtZmFtaWx5OkNvbnNvbGFzLCJOb3RvIFNhbnMgTW9ubyIsbW9ub3NwYWNlO2ZvbnQtc2l6ZToxOXB4O2ZpbGw6IzE2MzY0ZH0KICAgIDwvc3R5bGU+CiAgPC9kZWZzPgoKICA8cmVjdCBjbGFzcz0iYmciIHdpZHRoPSIxNjAwIiBoZWlnaHQ9IjI4NTAiLz4KICA8cmVjdCBmaWxsPSJ1cmwoI2hlYWRlcikiIHg9IjAiIHk9IjAiIHdpZHRoPSIxNjAwIiBoZWlnaHQ9IjE4NSIvPgogIDx0ZXh0IGNsYXNzPSJ0aXRsZSIgeD0iNzAiIHk9Ijc1Ij5TVE0zMiDmraXov5vnlLXmnLrvvJrmoq/lvaLmm7Lnur/kuI4gUyDlvaLliqDlh4/pgJ88L3RleHQ+CiAgPHRleHQgY2xhc3M9InN1YnRpdGxlIiB4PSI3MiIgeT0iMTI1Ij5TIOKGkiBmX3N0ZXAg4oaSIEFfc3RlcCDihpIgSl9zdGVwIOKGkiDOlHQg4oaSIHRpbWVyIHRpY2sg4oaSIEFSUiDihpIgU1RFUDwvdGV4dD4KICA8dGV4dCBjbGFzcz0ic3VidGl0bGUiIHg9IjcyIiB5PSIxNjAiPuaguOW/g++8muair+W9ouabsue6v+mZkOWItuWKoOmAn+W6pu+8jFMg5b2i5puy57q/6L+b5LiA5q2l6ZmQ5Yi2IGplcms8L3RleHQ+CgogIDxyZWN0IGNsYXNzPSJiYW5kIiB4PSI2MCIgeT0iMjIwIiB3aWR0aD0iMTQ4MCIgaGVpZ2h0PSIxMTAiIHJ4PSIxOCIvPgogIDx0ZXh0IGNsYXNzPSJoIiB4PSI4OCIgeT0iMjY1Ij4xLiDmjqfliLblr7nosaE8L3RleHQ+CiAgPHRleHQgY2xhc3M9ImZvcm11bGEiIHg9IjkwIiB5PSIzMDUiPlBVTC9TVEVQ77ya5pyJ5pWI6L655rK/5o6o6L+b5LiA5Liq5b6u5q2l44CARElS77ya5pa55ZCR44CARU5B77ya5L2/6IO9PC90ZXh0PgogIDx0ZXh0IGNsYXNzPSJib2R5IiB4PSI5MCIgeT0iMzU1Ij5TVE0zMiDpgJrluLjmjqfliLblpJbpg6jpqbHliqjlmajnmoQgU1RFUCDohInlhrLml7bpl7Tpl7TpmpTvvIzpqbHliqjlmajlhoXpg6jotJ/otKPnu5Xnu4TnlLXmtYHlkoznu4bliIbjgII8L3RleHQ+CgogIDxyZWN0IGNsYXNzPSJib3giIHg9IjYwIiB5PSIzOTAiIHdpZHRoPSIxNDgwIiBoZWlnaHQ9IjIxMCIgcng9IjE4Ii8+CiAgPHRleHQgY2xhc3M9ImgiIHg9Ijg4IiB5PSI0MzUiPjIuIOi/kOWKqOmHj+Wxgue6pzwvdGV4dD4KICA8dGV4dCBjbGFzcz0iZm9ybXVsYSIgeD0iMTEwIiB5PSI0ODUiPlMgIC0tZC9kdC0tPiAgZl9zdGVwICAtLWQvZHQtLT4gIEFfc3RlcCAgLS1kL2R0LS0+ICBKX3N0ZXA8L3RleHQ+CiAgPHRleHQgY2xhc3M9ImJvZHkiIHg9IjExMCIgeT0iNTMwIj5T77ya57Sv6K6hIFNURVAg6ISJ5Yay5pWw77ybZl9zdGVw77ya6ISJ5Yay6aKR546H77ybQV9zdGVw77ya6ISJ5Yay5Yqg6YCf5bqm77ybSl9zdGVw77yaamVya+OAgjwvdGV4dD4KICA8dGV4dCBjbGFzcz0iZm9ybXVsYSIgeD0iMTEwIiB5PSI1NzUiPkFfc3RlcCA9IGRmX3N0ZXAvZHTjgIDjgIDjgIDjgIDjgIAgSl9zdGVwID0gZEFfc3RlcC9kdDwvdGV4dD4KCiAgPHJlY3QgY2xhc3M9ImJveCIgeD0iNjAiIHk9IjY1MCIgd2lkdGg9IjcxMCIgaGVpZ2h0PSI2NTAiIHJ4PSIxOCIvPgogIDx0ZXh0IGNsYXNzPSJoIiB4PSI4OCIgeT0iNjk1Ij4zQS4g5qKv5b2i6YCf5bqm5puy57q/PC90ZXh0PgogIDx0ZXh0IGNsYXNzPSJzbWFsbCIgeD0iODgiIHk9IjczMCI+6YCf5bqm5YiG5q6157q/5oCn77yb5Yqg6YCf5bqm5YiG5q615oGS5a6a77ybamVyayDlnKjliIfmjaLlpITnqoHlj5g8L3RleHQ+CiAgPGcgdHJhbnNmb3JtPSJ0cmFuc2xhdGUoMTA1LDc3MCkiPgogICAgPGxpbmUgY2xhc3M9ImF4aXMiIHgxPSI1NSIgeTE9IjQ1MCIgeDI9IjYyMCIgeTI9IjQ1MCIvPgogICAgPGxpbmUgY2xhc3M9ImF4aXMiIHgxPSI1NSIgeTE9IjQ1MCIgeDI9IjU1IiB5Mj0iNTUiLz4KICAgIDxsaW5lIGNsYXNzPSJncmlkIiB4MT0iNTUiIHkxPSIxOTAiIHgyPSI2MjAiIHkyPSIxOTAiLz4KICAgIDxsaW5lIGNsYXNzPSJncmlkIiB4MT0iNTUiIHkxPSIzMjAiIHgyPSI2MjAiIHkyPSIzMjAiLz4KICAgIDxwYXRoIGNsYXNzPSJ0cmFwIiBkPSJNNTUgNDUwIEwyMjAgMTMwIEw0MzAgMTMwIEw2MjAgNDUwIi8+CiAgICA8dGV4dCBjbGFzcz0icGxvdGxhYmVsIiB4PSI5MCIgeT0iNDc1Ij5BQ0NFTDwvdGV4dD4KICAgIDx0ZXh0IGNsYXNzPSJwbG90bGFiZWwiIHg9IjI5MCIgeT0iMTE1Ij5DUlVJU0U8L3RleHQ+CiAgICA8dGV4dCBjbGFzcz0icGxvdGxhYmVsIiB4PSI0ODUiIHk9IjQ3NSI+REVDRUw8L3RleHQ+CiAgICA8dGV4dCBjbGFzcz0icGxvdGxhYmVsIiB4PSI1IiB5PSI2NSI+ZjwvdGV4dD4KICAgIDx0ZXh0IGNsYXNzPSJwbG90bGFiZWwiIHg9IjYyMCIgeT0iNDc1Ij50PC90ZXh0PgogIDwvZz4KICA8dGV4dCBjbGFzcz0iZm9ybXVsYSIgeD0iMTAwIiB5PSIxMjYwIj5BX3N0ZXAgPSArQV9tYXgg4oaSIDAg4oaSIOKIkkFfbWF4PC90ZXh0PgoKICA8cmVjdCBjbGFzcz0iYm94IiB4PSI4MzAiIHk9IjY1MCIgd2lkdGg9IjcxMCIgaGVpZ2h0PSI2NTAiIHJ4PSIxOCIvPgogIDx0ZXh0IGNsYXNzPSJoIiB4PSI4NTgiIHk9IjY5NSI+M0IuIFMg5b2i6YCf5bqm5puy57q/PC90ZXh0PgogIDx0ZXh0IGNsYXNzPSJzbWFsbCIgeD0iODU4IiB5PSI3MzAiPuWKoOmAn+W6pui/nue7reWPmOWMlu+8m2plcmsg5pyJ6ZmQ77yb5py65qKw5Yay5Ye75ZKM5oyv5Yqo5pu05bCPPC90ZXh0PgogIDxnIHRyYW5zZm9ybT0idHJhbnNsYXRlKDg3NSw3NzApIj4KICAgIDxsaW5lIGNsYXNzPSJheGlzIiB4MT0iNTUiIHkxPSI0NTAiIHgyPSI2MjAiIHkyPSI0NTAiLz4KICAgIDxsaW5lIGNsYXNzPSJheGlzIiB4MT0iNTUiIHkxPSI0NTAiIHgyPSI1NSIgeTI9IjU1Ii8+CiAgICA8bGluZSBjbGFzcz0iZ3JpZCIgeDE9IjU1IiB5MT0iMTkwIiB4Mj0iNjIwIiB5Mj0iMTkwIi8+CiAgICA8bGluZSBjbGFzcz0iZ3JpZCIgeDE9IjU1IiB5MT0iMzIwIiB4Mj0iNjIwIiB5Mj0iMzIwIi8+CiAgICA8cGF0aCBjbGFzcz0ic21vb3RoIiBkPSJNNTUgNDUwIEMxMDAgNDQ1IDE1MCAzOTAgMjA1IDI1MCBDMjQ1IDE1MCAyODUgMTMwIDMzNSAxMzAgQzM4NSAxMzAgNDI1IDE1MCA0NjUgMjUwIEM1MjAgMzkwIDU3MCA0NDUgNjIwIDQ1MCIvPgogICAgPHRleHQgY2xhc3M9InBsb3RsYWJlbCIgeD0iOTAiIHk9IjQ3NSI+Uy1BQ0NFTDwvdGV4dD4KICAgIDx0ZXh0IGNsYXNzPSJwbG90bGFiZWwiIHg9IjI3OCIgeT0iMTE1Ij5DUlVJU0U8L3RleHQ+CiAgICA8dGV4dCBjbGFzcz0icGxvdGxhYmVsIiB4PSI0NzgiIHk9IjQ3NSI+Uy1ERUNFTDwvdGV4dD4KICAgIDx0ZXh0IGNsYXNzPSJwbG90bGFiZWwiIHg9IjUiIHk9IjY1Ij5mPC90ZXh0PgogICAgPHRleHQgY2xhc3M9InBsb3RsYWJlbCIgeD0iNjIwIiB5PSI0NzUiPnQ8L3RleHQ+CiAgPC9nPgogIDx0ZXh0IGNsYXNzPSJmb3JtdWxhIiB4PSI4NzAiIHk9IjEyNjAiPkpfc3RlcO+8mitKIOKGkiAwIOKGkiDiiJJKIOKGkiAwIOKGkiDiiJJKIOKGkiAwIOKGkiArSjwvdGV4dD4KCiAgPHJlY3QgY2xhc3M9ImJveCIgeD0iNjAiIHk9IjEzNTAiIHdpZHRoPSIxNDgwIiBoZWlnaHQ9IjUzMCIgcng9IjE4Ii8+CiAgPHRleHQgY2xhc3M9ImgiIHg9Ijg4IiB5PSIxMzk1Ij40LiDkuIPmrrUgUyDlvaLov5Dliqjov4fnqIs8L3RleHQ+CiAgPGcgdHJhbnNmb3JtPSJ0cmFuc2xhdGUoMTAwLDE0NTApIj4KICAgIDxyZWN0IGZpbGw9IiM3YWExZTUiIHg9IjAiIHk9IjgwIiB3aWR0aD0iMTc1IiBoZWlnaHQ9Ijk1IiByeD0iMTAiLz4KICAgIDxyZWN0IGZpbGw9IiM0YzgyYmQiIHg9IjE4MCIgeT0iODAiIHdpZHRoPSIxNzUiIGhlaWdodD0iOTUiIHJ4PSIxMCIvPgogICAgPHJlY3QgZmlsbD0iIzNiOWI4MyIgeD0iMzYwIiB5PSI4MCIgd2lkdGg9IjE3NSIgaGVpZ2h0PSI5NSIgcng9IjEwIi8+CiAgICA8cmVjdCBmaWxsPSIjNzhiNDlmIiB4PSI1NDAiIHk9IjgwIiB3aWR0aD0iMTc1IiBoZWlnaHQ9Ijk1IiByeD0iMTAiLz4KICAgIDxyZWN0IGZpbGw9IiNlNGEwNTYiIHg9IjcyMCIgeT0iODAiIHdpZHRoPSIxNzUiIGhlaWdodD0iOTUiIHJ4PSIxMCIvPgogICAgPHJlY3QgZmlsbD0iI2QzN2Q0ZCIgeD0iOTAwIiB5PSI4MCIgd2lkdGg9IjE3NSIgaGVpZ2h0PSI5NSIgcng9IjEwIi8+CiAgICA8cmVjdCBmaWxsPSIjYjk1NzUyIiB4PSIxMDgwIiB5PSI4MCIgd2lkdGg9IjE3NSIgaGVpZ2h0PSI5NSIgcng9IjEwIi8+CiAgICA8dGV4dCBmaWxsPSIjZmZmIiBjbGFzcz0ibGFiZWwiIHg9IjcyIiB5PSIxMzgiPitKPC90ZXh0PgogICAgPHRleHQgZmlsbD0iI2ZmZiIgY2xhc3M9ImxhYmVsIiB4PSIyMzAiIHk9IjEzOCI+QW1heDwvdGV4dD4KICAgIDx0ZXh0IGZpbGw9IiNmZmYiIGNsYXNzPSJsYWJlbCIgeD0iNDEyIiB5PSIxMzgiPuKIkko8L3RleHQ+CiAgICA8dGV4dCBmaWxsPSIjZmZmIiBjbGFzcz0ibGFiZWwiIHg9IjU3NSIgeT0iMTM4Ij5DUlVJU0U8L3RleHQ+CiAgICA8dGV4dCBmaWxsPSIjZmZmIiBjbGFzcz0ibGFiZWwiIHg9Ijc4NSIgeT0iMTM4Ij7iiJJKPC90ZXh0PgogICAgPHRleHQgZmlsbD0iI2ZmZiIgY2xhc3M9ImxhYmVsIiB4PSI5NjUiIHk9IjEzOCI+4oiSQW1heDwvdGV4dD4KICAgIDx0ZXh0IGZpbGw9IiNmZmYiIGNsYXNzPSJsYWJlbCIgeD0iMTE1NSIgeT0iMTM4Ij4rSjwvdGV4dD4KICAgIDx0ZXh0IGNsYXNzPSJtb25vU21hbGwiIHg9Ijc4IiB5PSIyMDUiPjE8L3RleHQ+PHRleHQgY2xhc3M9Im1vbm9TbWFsbCIgeD0iMjU4IiB5PSIyMDUiPjI8L3RleHQ+PHRleHQgY2xhc3M9Im1vbm9TbWFsbCIgeD0iNDM4IiB5PSIyMDUiPjM8L3RleHQ+PHRleHQgY2xhc3M9Im1vbm9TbWFsbCIgeD0iNjE4IiB5PSIyMDUiPjQ8L3RleHQ+PHRleHQgY2xhc3M9Im1vbm9TbWFsbCIgeD0iNzk4IiB5PSIyMDUiPjU8L3RleHQ+PHRleHQgY2xhc3M9Im1vbm9TbWFsbCIgeD0iOTc4IiB5PSIyMDUiPjY8L3RleHQ+PHRleHQgY2xhc3M9Im1vbm9TbWFsbCIgeD0iMTE1OCIgeT0iMjA1Ij43PC90ZXh0PgogIDwvZz4KICA8dGV4dCBjbGFzcz0iZm9ybXVsYSIgeD0iMTAwIiB5PSIxNzQwIj5mX3BlYWsgPSBmXzAgKyBKIFRfasKyICsgQV9tYXggVF9hPC90ZXh0PgogIDx0ZXh0IGNsYXNzPSJmb3JtdWxhIiB4PSIxMDAiIHk9IjE3OTAiPlRfaiA9IEFfbWF4IC8gSjwvdGV4dD4KICA8dGV4dCBjbGFzcz0iYm9keSIgeD0iMTAwIiB5PSIxODQwIj7msqHmnInnrKwgNCDmrrXljIDpgJ/lubPlj7Dml7bvvIzmmK/kuInop5LlvaIgUyDmm7Lnur/vvJvku43lj6/og73kv53nlZnmgZLlrprliqDpgJ/luqblubPlj7DjgII8L3RleHQ+CgogIDxyZWN0IGNsYXNzPSJib3giIHg9IjYwIiB5PSIxOTMwIiB3aWR0aD0iMTQ4MCIgaGVpZ2h0PSI0NDAiIHJ4PSIxOCIvPgogIDx0ZXh0IGNsYXNzPSJoIiB4PSI4OCIgeT0iMTk3NSI+NS4g5YWz6ZSu5YWs5byPPC90ZXh0PgogIDx0ZXh0IGNsYXNzPSJmb3JtdWxhIiB4PSIxMDAiIHk9IjIwMzAiPkFfc3RlcCh0KSA9IEogdDwvdGV4dD4KICA8dGV4dCBjbGFzcz0iZm9ybXVsYSIgeD0iMTAwIiB5PSIyMDgwIj5mX3N0ZXAodCkgPSBmXzAgKyAxLzIgSiB0wrI8L3RleHQ+CiAgPHRleHQgY2xhc3M9ImZvcm11bGEiIHg9IjEwMCIgeT0iMjEzMCI+Uyh0KSA9IFNfMCArIGZfMCB0ICsgMS82IEogdMKzPC90ZXh0PgogIDx0ZXh0IGNsYXNzPSJmb3JtdWxhIiB4PSIxMDAiIHk9IjIxODAiPlNfYWNjID0gQV9tYXggVF9qwrIgKyAzLzIgQV9tYXggVF9qIFRfYSArIDEvMiBBX21heCBUX2HCsjwvdGV4dD4KICA8dGV4dCBjbGFzcz0iZm9ybXVsYSIgeD0iMTAwIiB5PSIyMjMwIj5TX3RvdGFsID0gMiBTX2FjYyArIGZfcGVhayBUX3Y8L3RleHQ+CiAgPHRleHQgY2xhc3M9ImZvcm11bGEiIHg9IjEwMCIgeT0iMjI4MCI+Zl9zdGVwID0gZl9jbnQgLyAoQVJSICsgMSks44CAQVJSID0gcm91bmQoZl9jbnQgzpR0KSDiiJIgMTwvdGV4dD4KICA8dGV4dCBjbGFzcz0ic21hbGwiIHg9IjEwMCIgeT0iMjMzMCI+5Lul5LiKIFNfYWNjIOWFrOW8j+WBh+iuvuWIneWni+mikeeOh+S4uiAw77yM5LiUIGplcmsg5q616L6+5YiwIEFfbWF444CCPC90ZXh0PgoKICA8cmVjdCBjbGFzcz0id2FybiIgeD0iNjAiIHk9IjI0MjAiIHdpZHRoPSIxNDgwIiBoZWlnaHQ9IjI0NSIgcng9IjE4Ii8+CiAgPHRleHQgY2xhc3M9ImgiIHg9Ijg4IiB5PSIyNDY1Ij42LiDmnKzor77nqIvmlbDlgLznpLrkvos8L3RleHQ+CiAgPHRleHQgY2xhc3M9ImZvcm11bGEiIHg9IjEwMCIgeT0iMjUxNSI+SiA9IDEyODAwIHB1bHNlL3PCs++8jEFfbWF4ID0gMzIwMCBwdWxzZS9zwrLvvIxUX2ogPSAwLjI1IHM8L3RleHQ+CiAgPHRleHQgY2xhc3M9ImZvcm11bGEiIHg9IjEwMCIgeT0iMjU2NSI+Zl9tYXggPSAxNjAwIEh677yMVF9hID0gMC4yNSBz77yMU19hY2MgPSA2MDAgcHVsc2U8L3RleHQ+CiAgPHRleHQgY2xhc3M9ImZvcm11bGEiIHg9IjEwMCIgeT0iMjYxNSI+U19hY2MgKyBTX2RlYyA9IDEyMDAgcHVsc2U8L3RleHQ+CiAgPHRleHQgY2xhc3M9ImJvZHkiIHg9IjEwMCIgeT0iMjY1MCI+MTAwMCBwdWxzZSDnmoTov5DliqjkuI3og73ovr7liLAgMTYwMCBIeu+8jOW6lOmHh+eUqOaXoOWMgOmAn+W5s+WPsOeahOS4ieinkuW9oiBTIOabsue6v++8jOWunumZheWzsOWAvOe6piAxNDMzIEh644CCPC90ZXh0PgoKICA8cmVjdCBmaWxsPSJ1cmwoI2hlYWRlcikiIHg9IjYwIiB5PSIyNzAwIiB3aWR0aD0iMTQ4MCIgaGVpZ2h0PSIxMDUiIHJ4PSIxOCIvPgogIDx0ZXh0IGNsYXNzPSJzdWJ0aXRsZSIgeD0iOTAiIHk9IjI3NDAiPlNUTTMyIOaJp+ihjOmTvu+8muinhOWIkiBKL0EvZiDihpIg6K6h566XIM6UdCDihpIg6L2s5o2iIHRpY2sg4oaSIOabtOaWsCBBUlIg4oaSIOWumuaXtuWZqOi+k+WHuiBTVEVQPC90ZXh0PgogIDx0ZXh0IGNsYXNzPSJzdWJ0aXRsZSIgeD0iOTAiIHk9IjI3NzUiPmNvbmZpcm1lZO+8muWFrOW8j+S4jui/nue7reaooeWei++8m3VudmVyaWZpZWTvvJrlhbfkvZMgU1RNMzIg5pe26ZKf44CB6amx5Yqo5Zmo6ISJ5a695ZKM5a6e5py66LSf6L2944CCPC90ZXh0Pgo8L3N2Zz4K" />

图像已经通过 Base64 数据直接嵌入本 Markdown 文件，复制笔记时不依赖外部 SVG 文件。

## 三、步距角、细分与每圈脉冲数

设：

- $\theta_{full}$：电机整步步距角；
- $M$：驱动器细分数；
- $\theta_{step}$：一个微步的机械角度；
- $N_{ppr}$：转一圈所需的 STEP 脉冲数。

$$
\theta_{step}=\frac{\theta_{full}}{M}
$$

$$
N_{ppr}=\frac{360^\circ}{\theta_{step}}
=\frac{360^\circ}{\theta_{full}}M
$$

对于 1.8° 电机和 1/16 细分：

$$
\theta_{step}
=\frac{1.8^\circ}{16}
=0.1125^\circ
$$

$$
N_{ppr}
=\frac{360^\circ}{0.1125^\circ}
=3200\ \text{pulse/rev}
$$

## 四、机械转速与 STEP 频率

$$
n_{rev}=\frac{n_{rpm}}{60}
$$

$$
\boxed{
f_{step}
=\frac{N_{ppr}n_{rpm}}{60}
}
$$

对于 30 rpm：

$$
f_{step}
=\frac{3200\times30}{60}
=1600\ \text{Hz}
$$

STEP 周期：

$$
\boxed{
T_{step}=\frac{1}{f_{step}}
}
$$

$$
T_{step}=\frac{1}{1600}=625\ \mu\text{s}
$$

## 五、STM32 定时器 PSC 与 ARR

PSC 分频后的实际计数频率：

$$
\boxed{
f_{cnt}=\frac{f_{timer}}{PSC+1}
}
$$

向上计数时，ARR 周期包含 ARR + 1 个 tick：

$$
\boxed{
f_{step}=\frac{f_{cnt}}{ARR+1}
}
$$

所以：

$$
\boxed{
ARR=
\frac{f_{timer}}
{(PSC+1)f_{step}}-1
}
$$

示例：$f_{timer}=72\ \text{MHz}$、PSC = 71：

$$
f_{cnt}
=\frac{72\ \text{MHz}}{72}
=1\ \text{MHz}
$$

$$
ARR+1=\frac{1\ \text{MHz}}{1600}=625
$$

$$
\boxed{ARR=624}
$$

## 六、运动量层级与 jerk

在 STEP 脉冲域中：

$$
S
\xrightarrow{\frac{d}{dt}}
f_{step}
\xrightarrow{\frac{d}{dt}}
A_{step}
\xrightarrow{\frac{d}{dt}}
J_{step}
$$

其中：

$$
\boxed{
A_{step}=\frac{df_{step}}{dt}
}
$$

$$
\boxed{
J_{step}=\frac{dA_{step}}{dt}
}
$$

梯形速度曲线主要限制 $A_{step}$；S 形曲线进一步限制 $J_{step}$。

## 七、恒定 jerk 段公式推导

假设初始加速度为 0，jerk 为常数 $J$：

$$
J=\frac{dA_{step}}{dt}
$$

积分得到：

$$
\boxed{
A_{step}(t)=Jt
}
$$

频率是加速度的积分：

$$
f_{step}(t)
=f_0+\int_0^t A_{step}(\tau)d\tau
$$

所以：

$$
\boxed{
f_{step}(t)
=f_0+\frac{1}{2}Jt^2
}
$$

累计脉冲数：

$$
S(t)
=S_0+\int_0^t f_{step}(\tau)d\tau
$$

得到：

$$
\boxed{
S(t)
=S_0+f_0t+\frac{1}{6}Jt^3
}
$$

## 八、完整 S 形加速前三段

<pre>第 1 段：+J，加速度从 0 增加到 Amax
第 2 段：J = 0，加速度保持 Amax
第 3 段：−J，加速度从 Amax 降回 0</pre>

jerk 段时间：

$$
\boxed{
T_j=\frac{A_{max}}{J}
}
$$

第 1 段：

$$
A_1(\tau)=J\tau
$$

$$
f_1(\tau)=f_0+\frac{1}{2}J\tau^2
$$

$$
S_1(\tau)=S_0+f_0\tau+\frac{1}{6}J\tau^3
$$

第 2 段：

$$
A_2(\tau)=A_{max}
$$

$$
f_2(\tau)=f_1+A_{max}\tau
$$

$$
S_2(\tau)
=S_1+f_1\tau+\frac{1}{2}A_{max}\tau^2
$$

第 3 段：

$$
A_3(\tau)=A_{max}-J\tau
$$

$$
f_3(\tau)
=f_2+A_{max}\tau-\frac{1}{2}J\tau^2
$$

$$
S_3(\tau)
=S_2+f_2\tau
+\frac{1}{2}A_{max}\tau^2
-\frac{1}{6}J\tau^3
$$

峰值频率：

$$
\boxed{
f_{peak}=f_0+JT_j^2+A_{max}T_a
}
$$

当 $f_0=0$ 时：

$$
\boxed{
f_{peak}=A_{max}(T_j+T_a)
}
$$

## 九、S 形加速段脉冲数

在初始频率为 0、能够达到 $A_{max}$ 的条件下：

$$
\boxed{
S_{acc}
=A_{max}T_j^2
+\frac{3}{2}A_{max}T_jT_a
+\frac{1}{2}A_{max}T_a^2
}
$$

对称减速时：

$$
S_{dec}=S_{acc}
$$

存在匀速段时：

$$
\boxed{
S_{total}
=S_{acc}+f_{peak}T_v+S_{dec}
}
$$

因此：

$$
\boxed{
T_v
=\frac{S_{total}-S_{acc}-S_{dec}}{f_{peak}}
}
$$

如果 $T_v\ge0$，是梯形 S 曲线；如果 $T_v<0$，是三角形 S 曲线。

## 十、本课程数值示例

设：

$$
J=12800\ \text{pulse/s}^3
$$

$$
A_{max}=3200\ \text{pulse/s}^2
$$

$$
f_{max}=1600\ \text{Hz}
$$

jerk 段时间：

$$
T_j=\frac{3200}{12800}=0.25\ \text{s}
$$

两个 jerk 段一共增加 800 Hz，因此恒定加速度段还需增加 800 Hz：

$$
T_a=\frac{800}{3200}=0.25\ \text{s}
$$

完整加速时间：

$$
T_{acc}=2T_j+T_a=0.75\ \text{s}
$$

加速段三个部分：

$$
S_1
=\frac{1}{6}\times12800\times0.25^3
\approx33.33\ \text{pulse}
$$

$$
S_2
=400\times0.25
+\frac{1}{2}\times3200\times0.25^2
=200\ \text{pulse}
$$

$$
S_3
=1200\times0.25+100-33.33
\approx366.67\ \text{pulse}
$$

所以：

$$
S_{acc}=33.33+200+366.67=600\ \text{pulse}
$$

对称减速：

$$
S_{dec}=600\ \text{pulse}
$$

达到 1600 Hz 至少需要：

$$
S_{acc}+S_{dec}=1200\ \text{pulse}
$$

因此总脉冲数为 1000 时：

$$
1000<1200
$$

不能达到 1600 Hz，必须使用三角形 S 曲线。

本例实际峰值频率约为：

$$
\boxed{
f_{peak}\approx1433\ \text{Hz}
}
$$

对应机械转速：

$$
\boxed{
n_{peak}
=\frac{60f_{peak}}{3200}
\approx26.87\ \text{rpm}
}
$$

## 十一、三角形 S 曲线的两种情况

### 1. 达到最大加速度，但没有匀速段

临界距离：

$$
\boxed{
S_{boundary}=2A_{max}T_j^2
}
$$

本例：

$$
S_{boundary}
=2\times3200\times0.25^2
=400\ \text{pulse}
$$

当：

$$
S_{total}>S_{boundary}
$$

可以达到 $A_{max}$，但不一定达到 $f_{max}$。

### 2. 连最大加速度也达不到

如果：

$$
S_{total}<S_{boundary}
$$

则使用四段纯 jerk 曲线：

<pre>+J → −J → −J → +J</pre>

此时：

$$
S_{total}=2JT_j^3
$$

所以：

$$
\boxed{
T_j=\sqrt[3]{\frac{S_{total}}{2J}}
}
$$

峰值频率：

$$
\boxed{
f_{peak}=JT_j^2
}
$$

## 十二、STM32 的离散执行

连续轨迹到定时器的转换链：

$$
f_{step}
\rightarrow \Delta t
\rightarrow c
\rightarrow ARR
$$

具体为：

$$
\Delta t_k=\frac{1}{f_{step,k}}
$$

$$
c_k=f_{cnt}\Delta t_k
$$

$$
\boxed{
ARR_k=\operatorname{round}(c_k)-1
}
$$

推荐的软件结构：

<pre>轨迹规划层：
输入总脉冲数、目标频率、Amax、Jmax
判断梯形 S 曲线或三角形 S 曲线
计算各阶段时间和边界

轨迹离散层：
计算每个脉冲的目标频率
转换为 Δt、tick 和 ARR
生成 ARR 查表

定时器执行层：
硬件输出 STEP
更新 ARR
更新 step_count
切换七段状态
完成后停止定时器</pre>

完整七段：

<pre>IDLE
→ DIR_SETUP
→ ENABLE
→ S_ACCEL_1
→ S_ACCEL_2
→ S_ACCEL_3
→ CRUISE
→ S_DECEL_1
→ S_DECEL_2
→ S_DECEL_3
→ STOP</pre>

不能简单对 ARR 做线性插值，因为：

$$
f_{step}=\frac{f_{cnt}}{ARR+1}
$$

正确流程是：

<pre>先规划 f_step
    ↓
计算 Δt
    ↓
计算 timer tick
    ↓
得到 ARR
    ↓
由硬件定时器输出 STEP</pre>

## 十三、最终公式总表

$$
\theta_{step}=\frac{\theta_{full}}{M}
$$

$$
N_{ppr}=\frac{360^\circ}{\theta_{step}}
$$

$$
f_{step}=\frac{N_{ppr}n_{rpm}}{60}
$$

$$
T_{step}=\frac{1}{f_{step}}
$$

$$
f_{cnt}=\frac{f_{timer}}{PSC+1}
$$

$$
f_{step}=\frac{f_{cnt}}{ARR+1}
$$

$$
ARR=\frac{f_{timer}}{(PSC+1)f_{step}}-1
$$

$$
A_{step}=\frac{df_{step}}{dt}
$$

$$
J_{step}=\frac{dA_{step}}{dt}
$$

$$
A_{step}(t)=Jt
$$

$$
f_{step}(t)=f_0+\frac{1}{2}Jt^2
$$

$$
S(t)=S_0+f_0t+\frac{1}{6}Jt^3
$$

$$
T_j=\frac{A_{max}}{J}
$$

$$
S_{acc}
=A_{max}T_j^2
+\frac{3}{2}A_{max}T_jT_a
+\frac{1}{2}A_{max}T_a^2
$$

$$
S_{total}=S_{acc}+f_{peak}T_v+S_{dec}
$$

$$
ARR_k=\operatorname{round}(f_{cnt}\Delta t_k)-1
$$

## 十四、证据边界

### confirmed

- 1.8° 电机、1/16 细分得到 3200 pulse/rev；
- 30 rpm 对应 1600 Hz；
- 1600 Hz 对应 625 μs；
- PSC + 1 和 ARR + 1 的定时器关系；
- 梯形曲线与 S 形曲线的连续运动学推导；
- jerk、加速度、STEP 频率和累计脉冲的积分关系；
- 本示例中 S 形加速段需要 600 pulse。

### user-confirmed

- 使用 STM32 控制步进驱动器；
- 熟悉 PWM、SPWM、SVPWM 的基本概念；
- 常用 EBF-MSD4805；
- 希望先掌握公式，再学习 STM32 实现。

### unverified

- 具体 STM32 型号的定时器时钟树和 APB 分频规则；
- EBF-MSD4805 当前版本的 PUL 有效边沿、输入电平、最小高低脉宽；
- DIR 建立时间和保持时间；
- 定时器预装载、更新事件和 ISR 最坏执行时间；
- 电机负载、供电、电流设置、最高可靠速度和实际丢步情况；
- 示波器或逻辑分析仪测得的真实 STEP 周期和方向时序。

## 十五、参考资料

- [Microchip AVR446：Linear speed control of stepper motor](https://ww1.microchip.com/downloads/en/appnotes/doc8017.pdf)
- [ST AN4013：Introduction to timers for STM32 MCUs](https://www.st.com/resource/en/application_note/an4013-introduction-to-timers-for-stm32-mcus-stmicroelectronics.pdf)
- [ST AN4776：General-purpose timer cookbook for STM32 microcontrollers](https://www.st.com/resource/en/application_note/an4776-generalpurpose-timer-cookbook-for-stm32-microcontrollers-stmelectronics.pdf)
- [野火 EBF-MSD4805 资料下载中心](https://doc.embedfire.com/products/link/zh/latest/module/motor/ebf_msd4805.html)

## 十六、自测题

1. 为什么 S 形曲线比梯形曲线多了 jerk 这一层？
2. 第 3 段中为什么 jerk 为负，但 ARR 仍然减小？
3. 如果 $S_{total}<S_{acc}+S_{dec}$，应该使用什么曲线？
4. 如果 $f_{cnt}=2\ \text{MHz}$、某个脉冲间隔为 5 ms，如何计算 tick 和 ARR？
5. 为什么不能直接对 ARR 做线性插值来生成 S 形速度曲线？
