#include "chibicc.h"

static int depth;

static void push(void) {
  printf("  st.d $a0, $sp, -%d\n", depth * 8);
  depth++;
}

static void pop(char *arg) {
  depth--;
  printf("  ld.d $%s, $sp, -%d\n", arg, depth * 8);
}

static void gen_expr(Node *node) {
  switch (node->kind) {
  case ND_NUM:
    printf("  li.d $a0, %d\n", node->val);
    return;
  case ND_NEG:
    gen_expr(node->lhs);
    printf("  sub.d $a0, $r0, $a0\n");
    return;
  }

  gen_expr(node->rhs);
  push();
  gen_expr(node->lhs);
  pop("a1");

  switch (node->kind) {
  case ND_ADD:
    printf("  add.d $a0, $a0, $a1\n");
    return;
  case ND_SUB:
    printf("  sub.d $a0, $a0, $a1\n");
    return;
  case ND_MUL:
    printf("  mul.d $a0, $a0, $a1\n");
    return;
  case ND_DIV:
    printf("  div.d $a0, $a0, $a1\n");
    return;
  case ND_EQ:
  case ND_NE:

    printf("  xor $a0, $a0, $a1\n");

    if (node->kind == ND_EQ)
      printf("  xori $a0, $a0, 1\n");
    else if (node->kind == ND_NE)
      printf("  andi $a0, $a0, 1\n");
    return;
  case ND_LT:
      printf("  slt $a0, $a0, $a1\n");
    return;
  case ND_LE:
      printf("  slt $a0, $a1, $a0\n");
      printf("  xori $a0, $a0, 1\n");
    return;
  default:
    break;
  }

  error("invalid expression");
}

void codegen(Node *node) {
  printf("  .globl main\n");
  printf("main:\n");

  gen_expr(node);
  printf("  jr $ra\n");

  assert(depth == 0);

  printf(".LFE0:\n");
  printf("  .size   main, .-main\n");
  printf("  .section  .note.GNU-stack,\"\",@progbits\n");
}
