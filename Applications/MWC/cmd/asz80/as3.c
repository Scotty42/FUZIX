/*
 * Z-80 assembler.
 * Read in expressions in
 * address fields.
 */
#include	"as.h"

#define	LOPRI	0
#define	ADDPRI	1
#define	MULPRI	2
#define	HIPRI	3

/*
 * Read in an address
 * descriptor, and fill in
 * the supplied "ADDR" structure
 * with the mode and value.
 * Exits directly to "qerr" if
 * there is no address field or
 * if the syntax is bad.
 */
void getaddr(ADDR *ap)
{
	int reg;
	int c;

	if ((c=getnb()) != '(') {
		unget(c);
		expr1(ap, LOPRI, 0);
		return;
	}
	expr1(ap, LOPRI, 1);
	if (getnb() != ')')
		qerr(BRACKET_EXPECTED);
	reg = ap->a_type&TMREG;
	switch (ap->a_type&TMMODE) {
	case TBR:
		if (reg != C)
			aerr(REG_MUST_BE_C);
		ap->a_type |= TMINDIR;
		break;
	case TSR:
	case TCC:
		aerr(ADDR_REQUIRED);
		break;
	case TUSER:
		ap->a_type |= TMINDIR;
		break;
	case TWR:
		if (reg == HL)
			ap->a_type = TBR|M;
		else if (reg==IX || reg==IY)
			ap->a_type = TBR|reg;
		else if (reg==AF || reg==AFPRIME)
			aerr(INVALID_REG);
		else
			ap->a_type |= TMINDIR;
	}
}

static void chkabsolute(ADDR *a)
{
	/* Not symbols, doesn't matter */
	if ((a->a_type & TMMODE) != TUSER)
		return;
	if (a->a_segment != ABSOLUTE)
		aerr(MUST_BE_ABSOLUTE);
}

static void chksegment(ADDR *left, ADDR *right, int op)
{
	/* Not symbols, doesn't matter */
	if ((left->a_type & TMMODE) != TUSER ||(right->a_type & TMMODE) != TUSER)
		return;

	/* Anything goes with absolutes */
	if (left->a_segment == ABSOLUTE && right->a_segment == ABSOLUTE)
		return;

	/* This relies on ABSOLUTE being 0, an addition of segment offset and
	   absolute either way around produces a segment offset */
	if ((left->a_segment == ABSOLUTE || right->a_segment == ABSOLUTE) &&
		op == '+') {
		if (left->a_sym)
			right->a_sym = left->a_sym;
		else if (right->a_sym)
			left->a_sym = right->a_sym;
		left->a_segment += right->a_segment;
		return;
	}
	/* Subtraction within segment produces an absolute */
	if (left->a_segment == right->a_segment && op == '-') {
		left->a_segment = ABSOLUTE;
		left->a_sym = NULL;
		return;
	}
	left->a_sym = NULL;
	aerr(MUST_BE_ABSOLUTE);
}

/*
 * Expression reader,
 * real work, part I. Read
 * operators and resolve types.
 * The "lpri" is the firewall operator
 * priority, which stops the scan.
 * The "paren" argument is true if
 * the expression is in parentheses.
 */
void expr1(ADDR *ap, int lpri, int paren)
{
	int c;
	int opri;
	ADDR right;

	expr2(ap);
	while ((c=getnb())=='+' || c=='-' || c=='*' || c=='/') {
		opri = ADDPRI;
		if (c=='*' || c=='/')
			opri = MULPRI;
		if (opri <= lpri)
			break;
		expr1(&right, opri, paren);
		switch (c) {
		case '+':
			if ((ap->a_type&TMMODE) != TUSER)
				istuser(&right);
			else
				ap->a_type = right.a_type;
			isokaors(ap, paren);
			chksegment(ap, &right, '+');
			ap->a_value += right.a_value;
			break;
		case '-':
			istuser(&right);
			isokaors(ap, paren);
			chksegment(ap, &right, '-');
			ap->a_value -= right.a_value;
			break;
		case '*':
			istuser(ap);
			istuser(&right);
			chksegment(ap, &right, '*');
			ap->a_value *= right.a_value;
			break;
		case '/':
			istuser(ap);
			istuser(&right);
			chksegment(ap, &right, '/');
			if (right.a_value == 0)
				err('z', DIVIDE_BY_ZERO);
			else
				ap->a_value /= right.a_value;
		}
	}
	unget(c);
}

/*
 * Expression reader,
 * real work, part II. Read
 * in terminals.
 */
void expr2(ADDR *ap)
{
	int c;
	SYM *sp;
	int mode;
	char id[NCPS];

	c = getnb();
	if (c == '[') {
		expr1(ap, LOPRI, 0);
		if (getnb() != ']')
			qerr(SQUARE_EXPECTED);
		return;
	}
	if (c == '-') {
		expr1(ap, HIPRI, 0);
		istuser(ap);
		chkabsolute(ap);
		ap->a_value = -ap->a_value;
		return;
	}
	if (c == '~') {
		expr1(ap, HIPRI, 0);
		istuser(ap);
		chkabsolute(ap);
		ap->a_value = ~ap->a_value;
		return;
	}
	if (c == '\'') {
		ap->a_type  = TUSER;
		ap->a_value = get();
		ap->a_segment = ABSOLUTE;
		while ((c=get()) != '\'') {
			if (c == '\n')
				qerr(PERCENT_EXPECTED);
			ap->a_value = (ap->a_value<<8) + c;
		}
		return;
	}
	if (c>='0' && c<='9' || c == '$') {
		expr3(ap, c);
		return;
	}
	if (isalpha(c)) {
		getid(id, c);
		if ((sp=lookup(id, uhash, 0)) == NULL
		&&  (sp=lookup(id, phash, 0)) == NULL)
			sp = lookup(id, uhash, 1);
		mode = sp->s_type&TMMODE;
		if (mode==TBR || mode==TWR || mode==TSR || mode==TCC) {
			ap->a_type  = mode|sp->s_value;
			ap->a_value = 0;
			return;
		}
		/* An external symbol has to be tracked and output in
		   the relocations. Any known internal symbol is just
		   encoded as a relocation relative to a segment */
		if (mode == TNEW) {
			uerr(id);
			ap->a_sym = sp;
		} else
			ap->a_sym = NULL;
		ap->a_type  = TUSER;
		ap->a_value = sp->s_value;
		ap->a_segment = sp->s_segment;
		return;
	}
	qerr(SYNTAX_ERROR);
}

/*
 * Read in a constant. The argument
 * "c" is the first character of the constant,
 * and has already been validated. The number is
 * gathered up (stopping on non alphanumeric).
 * The radix is determined, and the number is
 * converted to binary.
 */
void expr3(ADDR *ap, int c)
{
	char *np1;
	char *np2;
	int radix;
	VALUE value;
	char num[40];

	np1 = &num[0];
	do {
		if (isupper(c))
			c = tolower(c);
		*np1++ = c;
		c = *ip++;
	} while (isalnum(c));
	--ip;
	switch (*--np1) {
	case 'h':
		radix = 16;
		break;
	case 'o':
	case 'q':
		radix = 8;
		break;
	case 'b':
		radix = 2;
		break;
	default:
		radix = 10;
		++np1;
	}
	np2 = &num[0];
	value = 0;
	/* No trailing tag, so look for 0octab, 0xhex and $xxxx */
	if (radix == 10) {
		if (*np2 == '0') {
			radix = 8;
			np2++;
			if (*np2 == 'x') {
				radix = 16;
				np2++;
			}
		} else if (*np2 =='$') {
			radix = 16;
			np2++;
		}
	}
	while (np2 < np1) {
		if ((c = *np2++)>='0' && c<='9')
			c -= '0';
		else if (c>='a' && c<='f')
			c -= 'a'-10;
		else
			err('n', INVALID_CONSTANT);
		if (c >= radix)
			err('n', INVALID_CONSTANT);
		value = radix*value + c;
	}
	ap->a_type  = TUSER;
	ap->a_value = value;
	ap->a_segment = ABSOLUTE;
}

/*
 * Make sure that the
 * mode and register fields of
 * the type of the "ADDR" pointed to
 * by "ap" can participate in an addition
 * or a subtraction.
 */
void isokaors(ADDR *ap, int paren)
{
	int mode;
	int reg;

	mode = ap->a_type&TMMODE;
	if (mode == TUSER)
		return;
	if (mode==TWR && paren!=0) {
		reg = ap->a_type&TMREG;
		if (reg==IX || reg==IY)
			return;
	}
	aerr(ADDR_REQUIRED);
}