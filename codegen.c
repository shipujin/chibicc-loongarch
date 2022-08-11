#include "chibicc.h"

static int depth;
static char *argreg[] = {"a0", "a1", "a2", "a3", "a4", "a5", "a6", "a7"};
static Obj *current_fn;

static void gen_expr(Node *node);
static void gen_stmt(Node *node);

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
  switch (node->kind) {
  case ND_VAR:
    if (node->var->is_local) {
      // Local variable
      printf("  addi.d $a0, $fp, %d\n", node->var->offset - node->var->ty->size);
    } else {
      // Global variable
      printf("  la.local $a0, %s\n", node->var->name);
    }

    return;
  case ND_DEREF:
    gen_expr(node->lhs);
    return;
  }

  error_tok(node->tok, "not an lvalue");
}

// Load a value from where a0 is pointing to.
static void load(Type *ty) {
  if (ty->kind == TY_ARRAY) {
    // If it is an array, do not attempt to load a value to the
    // register because in general we can't load an entire array to a
    // register. As a result, the result of an evaluation of an array
    // becomes not the array itself but the address of the array.
    // This is where "array is automatically converted to a pointer to
    // the first element of the array in C" occurs.
    return;
  }

  if (ty->size == 1)
     printf("  ld.b $a0, $a0, 0\n");
  else
     printf("  ld.d $a0, $a0, 0\n");
}

// Store a0 to an address that the stack top is pointing to.
static void store(Type *ty) {
  pop("a1");

  if (ty->size == 1)
     printf("  st.b $a0, $a1, 0\n");
  else
     printf("  st.d $a0, $a1, 0\n");
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
    load(node->ty);
    return;
  case ND_DEREF:
    gen_expr(node->lhs);
    load(node->ty);
    return;
  case ND_ADDR:
    gen_addr(node->lhs);
    return;
  case ND_ASSIGN:
    gen_addr(node->lhs);
    push();
    gen_expr(node->rhs);
    store(node->ty);
    return;
  case ND_STMT_EXPR:
    for (Node *n = node->body; n; n = n->next)
      gen_stmt(n);
    return;
  case ND_FUNCALL: {
    int nargs = 0;
    for (Node *arg = node->args; arg; arg = arg->next) {
      gen_expr(arg);
      push();
      nargs++;
    }

    for (int i = nargs - 1; i >= 0; i--)
      pop(argreg[i]);

    printf("  bl %s\n", node->funcname);
    return;
  }
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

  error_tok(node->tok, "invalid expression");
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
    printf("  b .L.return.%s\n", current_fn->name);
    return;
  case ND_EXPR_STMT:
    gen_expr(node->lhs);
    return;
  }

  error_tok(node->tok, "invalid statement");
}

// Assign offsets to local variables.
static void assign_lvar_offsets(Obj *prog) {
  for (Obj *fn = prog; fn; fn = fn->next) {
    if (!fn->is_function)
      continue;

    int offset = 0;
    for (Obj *var = fn->locals; var; var = var->next) {
      var->offset = -offset;
      offset += var->ty->size;
    }
    fn->stack_size = align_to(offset, 16);
  }
}

static void emit_data(Obj *prog) {
  for (Obj *var = prog; var; var = var->next) {
    if (var->is_function)
      continue;

    printf("  .data\n");
    printf("  .globl %s\n", var->name);
    printf("%s:\n", var->name);

    if (var->init_data) {
      for (int i = 0; i < var->ty->size; i++)
        printf("  .byte %d\n", var->init_data[i]);
    } else {
      printf("  .zero %d\n", var->ty->size);
    }
  }
}

static void emit_text(Obj *prog) {
  for (Obj *fn = prog; fn; fn = fn->next) {
    if (!fn->is_function)
      continue;

    printf("  .globl %s\n", fn->name);
    printf("  .text\n");
    printf("%s:\n", fn->name);
    current_fn = fn;

    // Prologue
    printf("  addi.d $sp, $sp,-%d\n", fn->stack_size + 16);
    printf("  st.d $ra, $sp, %d\n", fn->stack_size + 8);
    printf("  st.d $fp, $sp, %d\n", fn->stack_size);
    printf("  add.d $fp, $r0, $sp\n");

    printf("  addi.d $sp, $sp, -%d\n", fn->stack_size);

    // Save passed-by-register arguments to the stack
    int i = 0;
    for (Obj *var = fn->params; var; var = var->next) {
      if (var->ty->size == 1)
         printf("  st.b $%s, $fp, %d\n", argreg[i++], var->offset - var->ty->size);
      else
         printf("  st.d $%s, $fp, %d\n", argreg[i++], var->offset - var->ty->size);
    }

    // Emit code
    gen_stmt(fn->body);
    assert(depth == 0);

    // Epilogue
    printf(".L.return.%s:\n", fn->name);
    printf("  add.d $sp, $r0, $fp\n");
    printf("  ld.d $ra, $sp, %d\n", fn->stack_size + 8);
    printf("  ld.d $fp, $sp, %d\n", fn->stack_size);
    printf("  addi.d $sp, $sp, %d\n", fn->stack_size + 16);
    printf("  jr $ra\n");
  }
}

void codegen(Obj *prog) {
  assign_lvar_offsets(prog);
  emit_data(prog);
  emit_text(prog);

  printf(".LFE0:\n");
  printf("  .size   main, .-main\n");
  printf("  .section  .note.GNU-stack,\"\",@progbits\n");
}
