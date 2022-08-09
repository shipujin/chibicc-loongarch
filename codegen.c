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

// Compute the absolute address of a given node.
// It's an error if a given node does not reside in memory.
static void gen_addr(Node *node) {
  if (node->kind == ND_VAR) {
    int offset = (node->name - 'a' + 1) * 8;
    printf("  addi.d $a0, $fp,%d\n", -offset);
    return;
  }

  error("not an lvalue");
}

// Generate code for a given node.
static void gen_expr(Node *node) {
  switch (node->kind) {
  case ND_NUM:
    printf("  li.d $a0, %d\n", node->val);
    return;
  case ND_NEG:
    gen_expr(node->lhs);
    printf("  sub.d $a0, $r0, $a0\n");
    return;
  case ND_VAR:
    gen_addr(node);
    printf("  ld.d $a0, $a0, 0\n");
    return;
  case ND_ASSIGN:
    gen_addr(node->lhs);
    push();
    gen_expr(node->rhs);
    pop("a1");
    printf("  st.d $a0, $a1, 0\n");
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

static void gen_stmt(Node *node) {
  if (node->kind == ND_EXPR_STMT) {
    gen_expr(node->lhs);
    return;
  }

  error("invalid statement");
}

void codegen(Node *node) {
  printf("  .globl main\n");
  printf("main:\n");

  // Prologue
  printf("  addi.d $sp, $sp, -224\n");
  printf("  st.d $fp, $sp, 216\n");
  printf("  st.d $ra, $sp, 208\n");
  printf("  add.d $fp, $r0, $sp\n");

  printf("  addi.d $sp, $sp, -208\n");

  for (Node *n = node; n; n = n->next) {
    gen_stmt(n);
    assert(depth == 0);
  }

  printf("  add.d $sp, $r0, $fp\n");
  printf("  ld.d $fp, $sp, 216\n");
  printf("  ld.d $ra, $sp, 208\n");
  printf("  addi.d $sp, $sp, 224\n");
  printf("  jr $ra\n");

  printf(".LFE0:\n");
  printf("  .size   main, .-main\n");
  printf("  .section  .note.GNU-stack,\"\",@progbits\n");
}
