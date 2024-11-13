typedef int64_t fixed_t;

#define FP_SHIFT_AMOUNT 16 // 소수점 이하 16비트 사용

#define I_TO_F(n) ((fixed_t)(n) << FP_SHIFT_AMOUNT)
#define F_TO_I(x) ((x) >= 0 ? \
    (((x) + (1 << (FP_SHIFT_AMOUNT - 1))) >> FP_SHIFT_AMOUNT) : \
    (((x) - (1 << (FP_SHIFT_AMOUNT - 1))) >> FP_SHIFT_AMOUNT))

#define ADD(x, y) ((x) + (y))
#define SUB(x, y) ((x) - (y))
#define ADD_INT(x, n) ((x) + ((fixed_t)(n) << FP_SHIFT_AMOUNT))
#define SUB_INT(x, n) ((x) - ((fixed_t)(n) << FP_SHIFT_AMOUNT))
#define MULTIPLY(x, y) ((fixed_t)(((int64_t)(x)) * (y) >> FP_SHIFT_AMOUNT))
#define MULTIPLY_INT(x, n) ((x) * (n))
#define DIVIDE(x, y) ((fixed_t)((((int64_t)(x)) * (1 << FP_SHIFT_AMOUNT)) / (y)))
#define DIVIDE_INT(x, n) ((x) / (n))
