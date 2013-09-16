
#ifndef _CCN_FIB_H_
#define _CCN_FIB_H_

#define CCN_FIB_LIFETIME ((~0U) >> 1)
#define CCN_FIB_MCASTTTL (-1)
#define OP_REG  0
#define OP_UNREG 1

static void init_data(struct ccn_charbuf *local_scope_template);
static int get_ccndid(struct ccn *h, struct ccn_charbuf *local_scope_template,unsigned char *ccndid);
extern int add_ccn_face(struct ccn *h, const char *uri, const char *address, const unsigned int p);
extern int delete_ccn_face(struct ccn *h, const char *uri, const char *address, const unsigned int p);
#endif
