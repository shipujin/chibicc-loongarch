#include "chibicc.h"

static int depth;

static int count(void) {
  static int i = 1;
  return i++;
}

static void push(void) {
  printf("  addi.d $sp, $sp, -8\n");
  printf("  st.d $a0, $sp, 0\n");
  depth++;
}

static void pop(char *arg) {
  printf("  ld.d $%s, $sp, 0\n", arg);
  printf("  addi.d $sp, $sp, 8\n");
  depth--;
}

// Round up `n` to the nearest multiple of `align`. For instance,
// align_to(5, 8) returns 8 and align_to(11, 8) returns 16.
static int align_to(int n, int align) {
  return (n + align - 1) / align * align;
}

// Compute the absolute address of a given node.
// It's an error if a given node does not reside in memory.
static void gen_addr(Node *node) {
  if (node->kind == ND_VAR) {
    printf("  addi.d $a0, $fp, %d\n", node->var->offset);
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
  switch (node->kind) {
  case ND_IF: {
    int c = count();
    gen_expr(node->cond);
    printf("  beqz $a0, .L.else.%d\n", c);
    gen_stmt(node->then);
    printf("  b .L.end.%d\n", c);
    printf(".L.else.%d:\n", c);
    if (node->els)
      gen_stmt(node->els);
    printf(".L.end.%d:\n", c);
    return;
  }
  case ND_FOR: {
    int c = count();
    if (node->init)
      gen_stmt(node->init);
    printf(".L.begin.%d:\n", c);
    if (node->cond) {
      gen_expr(node->cond);
      printf("  beqz $a0, .L.end.%d\n", c);
    }
    gen_stmt(node->then);
    if (node->inc)
      gen_expr(node->inc);
    printf("  b .L.begin.%d\n", c);
    printf(".L.end.%d:\n", c);
    return;
  }
  case ND_BLOCK:
    for (Node *n = node->body; n; n = n->next)
      gen_stmt(n);
    return;
  case ND_RETURN:
    gen_expr(node->lhs);
    printf("  b .L.return\n");
    return;
  case ND_EXPR_STMT:
    gen_expr(node->lhs);
    return;
  }

  error("invalid statement");
}

// Assign offsets to local variables.
static void assign_lvar_offsets(Function *prog) {
  int offset = 0;
  for (Obj *var = prog->locals; var; var = var->next) {
    var->offset = -offset;
    offset += 8;
  }
  prog->stack_size = align_to(offset, 16);
}

void codegen(Function *prog) {
  assign_lvar_offsets(prog);

  printf("  .globl main\n");
  printf("main:\n");

  // Prologue
  printf("  addi.d $sp, $sp,-%d\n", prog->stack_size + 16);
  printf("  st.d $fp, $sp, %d\n", prog->stack_size + 8);
  printf("  st.d $ra, $sp, %d\n", prog->stack_size);
  printf("  add.d $fp, $r0, $sp\n");

  printf("  addi.d $sp, $sp, -%d\n", prog->stack_size);

  gen_stmt(prog->body);
  assert(depth == 0);

  printf(".L.return:\n");
  printf("  add.d $sp, $r0, $fp\n");
  printf("  ld.d $fp, $sp, %d\n", prog->stack_size + 8);
  printf("  ld.d $ra, $sp, %d\n", prog->stack_size);
  printf("  addi.d $sp, $sp, %d\n", prog->stack_size + 16);
  printf("  jr $ra\n");

  printf(".LFE0:\n");
  printf("  .size   main, .-main\n");
  printf("  .section  .note.GNU-stack,\"\",@progbits\n");
}
