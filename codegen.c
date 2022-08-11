#include "chibicc.h"

static FILE *output_file;
static int depth;
static char *argreg[] = {"a0", "a1", "a2", "a3", "a4", "a5", "a6", "a7"};
static Obj *current_fn;

static void gen_expr(Node *node);
static void gen_stmt(Node *node);

static void println(char *fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  vfprintf(output_file, fmt, ap);
  va_end(ap);
  fprintf(output_file, "\n");
}

static int count(void) {
  static int i = 1;
  return i++;
}

static void push(void) {
  println("  addi.d $sp, $sp, -8");
  println("  st.d $a0, $sp, 0");
  depth++;
}

static void pop(char *arg) {
  println("  ld.d $%s, $sp, 0", arg);
  println("  addi.d $sp, $sp, 8");
  depth--;
}

// Round up `n` to the nearest multiple of `align`. For instance,
// align_to(5, 8) returns 8 and align_to(11, 8) returns 16.
int align_to(int n, int align) {
  return (n + align - 1) / align * align;
}

// Compute the absolute address of a given node.
// It's an error if a given node does not reside in memory.
static void gen_addr(Node *node) {
  switch (node->kind) {
  case ND_VAR:
    if (node->var->is_local) {
      // Local variable
      println("  addi.d $a0, $fp, %d", node->var->offset - node->var->ty->size);
    } else {
      // Global variable
      println("  la.local $a0, %s", node->var->name);
    }

    return;
  case ND_DEREF:
    gen_expr(node->lhs);
    return;
  case ND_COMMA:
    gen_expr(node->lhs);
    gen_addr(node->rhs);
    return;
  case ND_MEMBER:
    gen_addr(node->lhs);
    println("  addi.d $a0, $a0, %d", node->member->offset);
    return;
  }

  error_tok(node->tok, "not an lvalue");
}

// Load a value from where a0 is pointing to.
static void load(Type *ty) {
  if (ty->kind == TY_ARRAY || ty->kind == TY_STRUCT || ty->kind == TY_UNION) {
    // If it is an array, do not attempt to load a value to the
    // register because in general we can't load an entire array to a
    // register. As a result, the result of an evaluation of an array
    // becomes not the array itself but the address of the array.
    // This is where "array is automatically converted to a pointer to
    // the first element of the array in C" occurs.
    return;
  }

  if (ty->size == 1)
     println("  ld.b $a0, $a0, 0");
  else if (ty->size == 2)
     println("  ld.h $a0, $a0, 0");
  else if (ty->size == 4)
    println("  ld.w $a0, $a0, 0");
  else
     println("  ld.d $a0, $a0, 0");
}

// Store a0 to an address that the stack top is pointing to.
static void store(Type *ty) {
  pop("a1");

  if (ty->kind == TY_STRUCT || ty->kind == TY_UNION) {
    for (int i = 0; i < ty->size; i++) {
      println("  ld.b $a4, $a0, %d\n", i);
      println("  st.b $a4, $a1, %d\n", i);
    }
    return;
  }

  if (ty->size == 1)
     println("  st.b $a0, $a1, 0");
  else if (ty->size == 2)
     println("  st.h $a0, $a1, 0");
  else if (ty->size == 4)
    println("  st.w $a0, $a1, 0");
  else
     println("  st.d $a0, $a1, 0");
}

// Generate code for a given node.
static void gen_expr(Node *node) {
  println("  .loc 1 %d", node->tok->line_no);

  switch (node->kind) {
  case ND_NUM:
    println("  li.d $a0, %ld", node->val);
    return;
  case ND_NEG:
    gen_expr(node->lhs);
    println("  sub.d $a0, $r0, $a0");
    return;
  case ND_VAR:
  case ND_MEMBER:
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
  case ND_COMMA:
    gen_expr(node->lhs);
    gen_expr(node->rhs);
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

    println("  bl %s", node->funcname);
    return;
  }
  }

  gen_expr(node->rhs);
  push();
  gen_expr(node->lhs);
  pop("a1");

  switch (node->kind) {
  case ND_ADD:
    println("  add.d $a0, $a0, $a1");
    return;
  case ND_SUB:
    println("  sub.d $a0, $a0, $a1");
    return;
  case ND_MUL:
    println("  mul.d $a0, $a0, $a1");
    return;
  case ND_DIV:
    println("  div.d $a0, $a0, $a1");
    return;
  case ND_EQ:
  case ND_NE:

    println("  xor $a0, $a0, $a1");

    if (node->kind == ND_EQ)
      println("  xori $a0, $a0, 1");
    else if (node->kind == ND_NE)
      println("  andi $a0, $a0, 1");
    return;
  case ND_LT:
      println("  slt $a0, $a0, $a1");
    return;
  case ND_LE:
      println("  slt $a0, $a1, $a0");
      println("  xori $a0, $a0, 1");
    return;
  default:
    break;
  }

  error_tok(node->tok, "invalid expression");
}

static void gen_stmt(Node *node) {
  println("  .loc 1 %d", node->tok->line_no);
  switch (node->kind) {
  case ND_IF: {
    int c = count();
    gen_expr(node->cond);
    println("  beqz $a0, .L.else.%d", c);
    gen_stmt(node->then);
    println("  b .L.end.%d", c);
    println(".L.else.%d:", c);
    if (node->els)
      gen_stmt(node->els);
    println(".L.end.%d:", c);
    return;
  }
  case ND_FOR: {
    int c = count();
    if (node->init)
      gen_stmt(node->init);
    println(".L.begin.%d:", c);
    if (node->cond) {
      gen_expr(node->cond);
      println("  beqz $a0, .L.end.%d", c);
    }
    gen_stmt(node->then);
    if (node->inc)
      gen_expr(node->inc);
    println("  b .L.begin.%d", c);
    println(".L.end.%d:", c);
    return;
  }
  case ND_BLOCK:
    for (Node *n = node->body; n; n = n->next)
      gen_stmt(n);
    return;
  case ND_RETURN:
    gen_expr(node->lhs);
    println("  b .L.return.%s", current_fn->name);
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
      offset = align_to(offset, var->ty->align);
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

    println("  .data");
    println("  .globl %s", var->name);
    println("%s:", var->name);

    if (var->init_data) {
      for (int i = 0; i < var->ty->size; i++)
        println("  .byte %d", var->init_data[i]);
    } else {
      println("  .zero %d", var->ty->size);
    }
  }
}

static void store_gp(int r, int offset, int sz) {
  switch (sz) {
  case 1:
    println("  st.b $%s, $fp, %d", argreg[r], offset - sz);
    return;
  case 2:
    println("  st.h $%s, $fp, %d", argreg[r], offset - sz);
    return;
  case 4:
    println("  st.w $%s, $fp, %d", argreg[r], offset - sz);
    return;
  case 8:
    println("  st.d $%s, $fp, %d", argreg[r], offset - sz);
    return;
  }
  unreachable();
}

static void emit_text(Obj *prog) {
  for (Obj *fn = prog; fn; fn = fn->next) {
    if (!fn->is_function || !fn->is_definition)
      continue;

    println("  .globl %s", fn->name);
    println("  .text");
    println("%s:", fn->name);
    current_fn = fn;

    // Prologue
    println("  addi.d $sp, $sp,-%d", fn->stack_size + 16);
    println("  st.d $ra, $sp, %d", fn->stack_size + 8);
    println("  st.d $fp, $sp, %d", fn->stack_size);
    println("  add.d $fp, $r0, $sp");

    println("  addi.d $sp, $sp, -%d", fn->stack_size);

    // Save passed-by-register arguments to the stack
    int i = 0;
    for (Obj *var = fn->params; var; var = var->next)
      store_gp(i++, var->offset, var->ty->size);

    // Emit code
    gen_stmt(fn->body);
    assert(depth == 0);

    // Epilogue
    println(".L.return.%s:", fn->name);
    println("  add.d $sp, $r0, $fp");
    println("  ld.d $ra, $sp, %d", fn->stack_size + 8);
    println("  ld.d $fp, $sp, %d", fn->stack_size);
    println("  addi.d $sp, $sp, %d", fn->stack_size + 16);
    println("  jr $ra");
  }
}

void codegen(Obj *prog, FILE *out) {
  output_file = out;

  assign_lvar_offsets(prog);
  emit_data(prog);
  emit_text(prog);

  println(".LFE0:");
  println("  .size   main, .-main");
  println("  .section  .note.GNU-stack,\"\",@progbits");
}
