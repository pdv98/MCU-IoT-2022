#define F_CPU	16000000	// CPU 진동수 16MHz 설정
#include <avr/io.h>			// 레지스터 설정을 위한 라이브러리
#include <avr/interrupt.h>	// 인터럽트 사용을 위한 라이브러리
#include <util/delay.h>		// 딜레이 함수 사용을 위한 라이브러리

#define STOP	0		// 정지는 mode 0
#define START	1		// 동작은 mode 1
#define INIT	2		// 초기화는 mode 2

#define C1	523		// 도
#define D1	587		// 레
#define E1	659		// 미
#define F1	699		// 파
#define G1	784		// 솔
#define A1	880		// 라
#define B1	988		// 시
#define C2	C1*2	// 도
#define DLY_4	4	// 4분 음표

// mode selector
unsigned char mode_sel=0;	// 시작 mode 0 설정

// display
const unsigned char Segment_Data[] =
{0x3F,0x06,0x5B,0x4F,0x66,0x6D,0x7D,0x27,0x7F,0x6F};		// 순서대로 0, 1, 2, 3, 4, 5, 6 ,7, 8, 9
char COLUMN[4]={0,0,0,0};	// 세븐세그먼트 4자리 수 변수

// clock
unsigned char count_int=0;	// 카운트 변수 (0~255)
unsigned int Seconds=0, Minutes=0, Hours=0;	// 초, 분, 시간 초기화

// stopwatch
int state=STOP;	// 초기상태는 정지상태로 설정
int SHOW_NUMBER=0, SHOW_NUMBER12=0, SHOW_NUMBER34=0;	// 초기 값 0 설정, 분/초 부분도 0으로 설정

// piano
volatile int Doremi[8]={C1,D1,E1,F1,G1,A1,B1,C2}; 	// 도, 레, 미, 파, 솔, 라, 시, 도
volatile int Doremi_length[8]={DLY_4,DLY_4,DLY_4,DLY_4,DLY_4,DLY_4,DLY_4,DLY_4}; 	// 4분음표
volatile unsigned char TIMERvalue=0xFF;	// TIMERvalue 초기값 1111 1111 = 255
volatile int freq = 1000, i,j;	// freq 초기값 1000, i, j 변수 선언

// timer
void Run_timer(void);

//tuner
volatile int fvar;
unsigned int readConvertData(void); // ADC 변환값
unsigned int adc_data;

// display
void Show_Display(unsigned int number);
void Show_stop_watch_Display(int number);
void ShowDigit(int i, int digit);

// mode
void mode0_clock();
void mode1_stop_watch();
void mode2_piano();
void mode3_timer();
void mode4_tuner();

// stopwatch
void Run_stop_watch(void);

// piano
void Shimpyo(int time);
void Cutoff_Play(void);
void piano_up(void);

// tuner
void Run_tuner(void);
void adc_init(void);        // ADC 설정을 위한 함수
void startConvertion(void); // ADV 시작
void Cutoff_tuner(void);

ISR(TIMER0_OVF_vect)		// 타이머0 오버플로우 인터럽트 발생시
{
	count_int++;		    // 카운트 값 1씩 증가
	if(count_int == 244)	// 244번 카운트 값 증가한 경우
	{
		if(mode_sel == 0) PORTG ^= 0x03;	// 0000 0011: PG0, PG1 LED toggle
		Seconds++;	        // 1초 증가
		count_int=0;	    // 카운트 변수 초기화
	}
	
}

ISR(TIMER2_OVF_vect)		// 타이머2 오버플로우 인터럽트 발생시
{
	if(mode_sel==2 || mode_sel==4)		// 현재 mode가 2 또는 4이면
	{
		TCNT2 = TIMERvalue;	// TIMERvalue부터 시작하여 256이 되면 인터럽트 발생
		PORTB ^= 0x10;	    // 0001 0000: PB4 toggle
	}
}

ISR(INT0_vect)			    // S0가 눌렸을 때
{
	if(++mode_sel>4)		// 현재 mode + 1이 3을 초과하면
	{
		mode_sel=0;		    // mode 0으로 초기화
	}
}

ISR(INT1_vect)			    // S1이 눌렸을 때
{
	if(mode_sel==1 || mode_sel==3)		// 현재 mode가 1 또는 3이면
	{
		if(state==STOP) state=START;	// 정지 상태인 경우 동작
		else            state=STOP;		// 정지 이외의 경우 정지
	}
}

ISR(INT2_vect)			    // S2가 눌렸을 때
{
	if(mode_sel==1 || mode_sel==3)		// 현재 mode가 1 또는 3이면
	{
		state=INIT;	        // 초기화
	}
}

int main(void)
{
	DDRA = 0xff;	// 1111 1111, PA0~PA7: output
	DDRB = 0x10;	// 0001 0000, PB4: output, 나머지 input
	DDRC = 0xff;	// 1111 1111, PC0~PC7: output
	DDRG = 0x03;	// 0000 0011, PG0, 1: output, PG2~PG7: input
	DDRF = 0x00;	// 1111 1111, PF0~PF7: output
	DDRE = 0xff;	// 1111 1111, PE0~PE7: output

	TCCR0 = 0x06;	// 0000 0110
	// 비트7: 강제 출력비교x(PWM 아닌 경우에만 유효)
	// 비트3, 6: Normal mode
	// 비트5, 4: OC단자 차단, 평소와 같은 핀으로 사용(출력비교x)
	// 비트 2~0: T0 핀을 이용한 카운터 동작, 하강 에지에서
	TCNT0 = 0x00;	// 0부터 시작해서 256이 되면 타이머 인터럽트 발생
	TCCR2 = 0x04;	// 0000 0100
	// 비트7: 강제 출력비교x (PWM 아닌 경우에만 유효)
	// 비트3, 6: Normal mode
	// 비트5, 4: OC단자 차단, 평소와 같은 핀으로 사용 (출력비교x)
	// 비트 2~0: 64분주로 동작
	// 16M/64 = 250kHz
	TCNT2 = 0x06;	// 6부터 시작해서 256이 되면 타이머 인터럽트 발생
	TIMSK = 0x41;	// 0100 0001
	// 2의 출력비교 인터럽트, 0의 오버플로우 인터럽트 허용
	EICRA=0xff;	// 1111 1111: Rising edge detection on INT0~INT3
	EIMSK=0x0f;	// 0000 1111: Set INT0~INT3
	
	SREG |= 0x80;	// Global interrupt 허용
	
	while (1)	// 무한 loop
	{
		switch(mode_sel)
		{
			case 0:			    // mode_sel == 0이면
			mode0_clock();		// mode0_clock() 실행
			break;
			case 1:			    // mode_sel == 1이면
			mode1_stop_watch();	// mode1_stop_watch() 실행
			break;
			case 2:			    // mode_sel == 2이면
			mode2_piano();		// mode2_piano() 실행
			break;
			case 3:			    // mode_sel == 3이면
			mode3_timer();		// mode3_timer() 실행
			break;
			case 4:			    // mode_sel == 4이면
			mode4_tuner();		// mode4_tuner() 실행
			break;
			default:
			mode0_clock();		// mode0_clock() 실행
			break;
		}
	}
}

void Show_Display(unsigned int number)
{
	COLUMN[0]   = (Minutes%100)/10;	// 분의 십의 자리 계산
	COLUMN[1]   = (Minutes%10);		// 분의 일의 자리 계산
	COLUMN[2]   = (Seconds%100)/10;	// 초의 십의 자리 계산
	COLUMN[3]   = (Seconds%10);		// 초의 일의 자리 계산
	
	for(int i=0; i<4; i++)			// 4자리 세븐세그먼트 설정
	{
		ShowDigit(COLUMN[i],i);		// 한 자릿수씩 숫자를 표현
		_delay_ms(2); 			    // 2ms delay
	}
}

void Show_stop_watch_Display(int number)	// 각 자리수가 무엇인지 계산하여 세븐세그먼트에 number 출력
{
	COLUMN[0] = number/1000;		// number의 천의 자리수 계산
	COLUMN[1] = (number%1000)/100;	// number의 백의 자리수 계산
	COLUMN[2] = (number%100)/10;	// number의 십의 자리수 계산
	COLUMN[3] = (number%10);		// number의 일의 자리수 계산
	for(int i=0; i<4; i++){			// 4자리 세븐세그먼트 설정
		ShowDigit(COLUMN[i],i);		// 한 자릿수씩 숫자를 표현
		_delay_ms(2);			    // 2ms delay
	}
}

void ShowDigit(int i, int digit)	    // 각 자리수의 세븐세그먼트 출력
{
	PORTC=~(0x01<<digit);			    // digit번째 세븐세그먼트만 변경 가능
	if(mode_sel==1 || mode_sel==3)	    // 현재 mode 1 또는 3이면
	{
		if(digit==1)
		PORTA = Segment_Data[i]|0x80;	// dot을 표시하기 위한 연산, 세븐세그먼트 숫자 변경
		else
		PORTA = Segment_Data[i];	    // 세븐세그먼트 숫자 변경
	}
	else PORTA = Segment_Data[i];	    // 세븐세그먼트 숫자 변경
}

void Run_stop_watch(void)	            // 스탑와치로 돌아가게 하는 함수
{
	switch(state)		                // state 값에 따른 함수
	{
		case STOP :	break;			    // 정지 상태이면 함수 나가기
		case START: SHOW_NUMBER34++;	// 동작 상태이면 0.01초씩 증가
		if(SHOW_NUMBER34>99)		    // 0.01초씩 증가해서 1초가 되면
		{
			SHOW_NUMBER12++;		    // 초 부분 1 증가
			if(SHOW_NUMBER12>99) SHOW_NUMBER12=0;	// 초 부분이 100이 되면 0으로 초기화
			SHOW_NUMBER34=0;		    // 뒷 부분 0으로 초기화
		}
		break;
		case INIT : SHOW_NUMBER12=0, SHOW_NUMBER34=0, state=STOP;
		// 초기화 상태의 경우, 모두 0으로 초기화
		break;
	}
}

void Run_timer(void)
{
	PORTE = 0x00;	    // PORTE를 초기화
	switch(state)       // state를 변수로 하는 switch 조건문
	{
		case STOP : break;              // state가 STOP이면 loop 탈출
		case START : SHOW_NUMBER34--;   // SHOW_NUMBER34 1씩 감소
		if (SHOW_NUMBER34<0)            // SHOW_NUMBER34가 0보다 작으면
		{
			SHOW_NUMBER12--;            // SHOW_NUMBER12 1씩 감소
			if (SHOW_NUMBER34<0) SHOW_NUMBER34=99;
			// SHOW_NUMBER34가 0보다 작으면 99로 설정
		}
		break; // loop 탈출
		case INIT : SHOW_NUMBER12=10, SHOW_NUMBER34=00, state=STOP;
		// state가 INIT이면 SHOW_NUMBER12=10, SHOW_NUMBER34=0, state=STOP 설정
		break; // loop 탈출
	}
	if (SHOW_NUMBER==1)	// SHOW_NUMBER가 1이면
	{
		state = STOP;	// STOP
		PORTE = 0x10;	// buzzer on
		_delay_ms(300);	// 300ms 유지
		PORTE = 0x00;	// buzzer off
		_delay_ms(200);	// 200ms 유지
		PORTE = 0x10;	// buzzer on
		_delay_ms(300);	// 300ms 유지
		PORTE = 0x00;	// buzzer off
		state = INIT;	// 초기화
	}
}

void piano_up(void)
{
	for(int i=0; i<8; i++)		// 8개의 음 출력
	{
		freq = Doremi[i];		// Doremi의 i번째 음
		TIMERvalue = 255-(1000000/(8*freq));	// TIMERvalue 계산
		Shimpyo(Doremi_length[i]);	// Doremi의 i번째 음 길이
		Cutoff_Play();			// 한 음씩 띄어서 듣기 위해
	}
}

void Shimpyo(int time)		// 각 음을 유지하는 시간
{
	for(int i=0; i<time; i++)
	{
		_delay_ms(50);	    // 50ms*time delay
	}
}

void Cutoff_Play(void)		// 한 음씩 띄어서 듣기 위해
{
	_delay_ms(300);		// 300ms delay
	TIMERvalue=255;	    // 켜져있던 스피커를 끔 (speaker toggle)
	_delay_ms(20);		// 20ms delay
}

void Run_tuner (void)
{
	while(1)
	{
		fvar = adc_data; // 진동수 설정
		TIMERvalue = 255 - (1000000/ (8*fvar)); // 몇 번 ON/OFF변경할 것인지 결정.
		break;
	}
}

//ADC initialize
void adc_init(void)
{
	ADCSRA = 0x00;  // ADCSRA = 0000 0000: disable adc
	ADMUX = 0x00;   // ADMUX  = 0000 0000: select adc input 0
	ACSR = 0x80;    // ACSR   = 1000 0000
	ADCSRA = 0xc7;  // ADCSRA = 1100 0111
}

// 입력으로 들어오는 채널의 ADC를 시작시킨다.
void startConvertion(void)
{
	ADCSRA = 0x87;  // ADC 모든 동작 허용, ADC 변환 시작, 분주비 128 설정
	ADMUX = 0x00;   // 단일변환모드에서 ADC0 입력, AREF 기준전압
	ADCSRA = ADCSRA | 0xc7;     // ADC 모든 동작 허용, ADC 변환 시작, 분주비 128 설정
}

// startConvertion() 후에 수행되며 변환된 값을 Return 한다.
unsigned int readConvertData(void)
{
	volatile unsigned int temp;     // ADC 변환값 저장 변수
	while ((ADCSRA & 0x10) == 0);   // ADCSRA의 비트4가 1이면 while문 종료 (ADC 변환 완료)
	// ADCSRA의 비트4가 0이면 while문 무한반복
	ADCSRA = ADCSRA | 0x10;         // ADCSRA의 비트4를 1로 만들어 ADC 되지 않도록 설정
	temp = (int)ADCL+(int)ADCH*256; // ADC 값 저장
	ADCSRA = ADCSRA | 0x10;         // ADCSRA의 비트4를 1로 만들어 ADC 되지 않도록 설정
	return temp;                    // ADC 변환 값 return
}

void mode0_clock()
{
	Show_Display(Seconds);
	if(Seconds>=60)		// 60초가 되면
	{
		Seconds=0;		// 0초로 초기화
		Minutes++;		// 1분 증가
	}
	if(Minutes>=60)		// 60분이 되면
	{
		Minutes=0;		// 0분으로 초기화
		Hours++;		// 1시간 증가
	}
	if(Hours>=24)		// 24시간이 되면
	{
		Hours=0;		// 0시로 초기화
	}
}

void mode1_stop_watch()
{
	PORTG=0x00;	                                    // PG0, PG1 LED off
	Run_stop_watch();	                            // Run_stop_watch 실행
	SHOW_NUMBER=SHOW_NUMBER12*100+SHOW_NUMBER34;    // 분초 세븐세그먼트에 나타내기 위한 계산
	Show_stop_watch_Display(SHOW_NUMBER);	        // SHOW_NUMBER를 세븐세그먼트에 출력
}

void mode2_piano()
{
	piano_up();		    // piano_up 실행
	_delay_ms(1000);	// 1s delay
}

void mode3_timer()
{
	Run_timer();		                            // Run_timer 실행
	SHOW_NUMBER=SHOW_NUMBER12*100+SHOW_NUMBER34;    // 분초 세븐세그먼트에 나타내기 위한 계산
	Show_stop_watch_Display(SHOW_NUMBER);	        // SHOW_NUMBER를 세븐세그먼트에 출력
}

void mode4_tuner()
{
	adc_init();                             // adc_init 실행
	startConvertion();                      // startConvertion 함수 실행
	adc_data = readConvertData();           // 읽어온 값을 adc_data로 저장
	Run_tuner();                            // Run_tuner 실행
	if(fvar<900 && fvar>860) PORTG=0x03;    // fvar이 860초과 900미만이면 PG0, PG1 LED on
	else PORTG=0x00;	                    // else PG0, PG1 LED off
}