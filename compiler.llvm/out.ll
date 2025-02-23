; ModuleID = 'tests/foo4.ll'
source_filename = "TestExamples/fib.c"
target datalayout = "e-m:e-p270:32:32-p271:32:32-p272:64:64-i64:64-f80:128-n8:16:32:64-S128"
target triple = "x86_64-unknown-linux-gnu"

; Function Attrs: noinline nounwind optnone uwtable
define dso_local i64 @fib(i64 noundef %n) #0 {
entry:
  %retval = alloca i64, align 8
  %n.addr = alloca i64, align 8
  %x = alloca i64, align 8
  %y = alloca i64, align 8
  %z = alloca i64, align 8
  %w = alloca i64, align 8
  %a = alloca i64, align 8
  store i64 %n, ptr %n.addr, align 8
  %0 = load i64, ptr %n.addr, align 8
  %cmp = icmp slt i64 %0, 2
  br i1 %cmp, label %if.then, label %if.end

if.then:                                          ; preds = %entry
  %1 = load i64, ptr %n.addr, align 8
  store i64 %1, ptr %retval, align 8
  br label %return.clone

if.end:                                           ; preds = %entry
  %2 = load i64, ptr %n.addr, align 8
  %sub = sub nsw i64 %2, 1
  detach within none, label %det.achd, label %det.cont

det.achd:                                         ; preds = %if.end
  %call = call i64 @fib(i64 noundef %sub)
  store i64 %call, ptr %x, align 8
  reattach within none, label %det.cont

det.cont:                                         ; preds = %det.achd, %if.end
  %3 = load i64, ptr %n.addr, align 8
  %sub1 = sub nsw i64 %3, 2
  detach within none, label %det.achd2, label %det.cont4

det.achd2:                                        ; preds = %det.cont
  %call3 = call i64 @fib(i64 noundef %sub1)
  store i64 %call3, ptr %y, align 8
  reattach within none, label %det.cont4

det.cont4:                                        ; preds = %det.achd2, %det.cont
  sync within none, label %sync.continue

sync.continue:                                    ; preds = %det.cont4
  %4 = load i64, ptr %x, align 8
  %sub5 = sub nsw i64 %4, 1
  detach within none, label %det.achd6, label %det.cont8

det.achd6:                                        ; preds = %sync.continue
  %call7 = call i64 @fib(i64 noundef %sub5)
  store i64 %call7, ptr %a, align 8
  reattach within none, label %det.cont8

det.cont8:                                        ; preds = %det.achd6, %sync.continue
  %5 = load i64, ptr %n.addr, align 8
  %sub9 = sub nsw i64 %5, 3
  detach within none, label %det.achd10, label %det.cont12

det.achd10:                                       ; preds = %det.cont8
  %call11 = call i64 @fib(i64 noundef %sub9)
  store i64 %call11, ptr %z, align 8
  reattach within none, label %det.cont12

det.cont12:                                       ; preds = %det.achd10, %det.cont8
  sync within none, label %sync.continue13

sync.continue13:                                  ; preds = %det.cont12
  %6 = load i64, ptr %x, align 8
  %7 = load i64, ptr %y, align 8
  %add = add nsw i64 %6, %7
  %8 = load i64, ptr %a, align 8
  %9 = load i64, ptr %z, align 8
  %mul = mul nsw i64 %8, %9
  %add14 = add nsw i64 %add, %mul
  store i64 %add14, ptr %w, align 8
  %10 = load i64, ptr %w, align 8
  store i64 %10, ptr %retval, align 8
  sync within none, label %sync.continue15

sync.continue15:                                  ; preds = %sync.continue13
  br label %return

return:                                           ; preds = %sync.continue15
  sync within none, label %sync.continue16

sync.continue16:                                  ; preds = %return.clone, %return
  %11 = load i64, ptr %retval, align 8
  ret i64 %11

return.clone:                                     ; preds = %if.then
  sync within none, label %sync.continue16
}

; Function Attrs: nounwind willreturn memory(argmem: readwrite)
declare token @llvm.syncregion.start() #1

; Function Attrs: nounwind willreturn memory(argmem: readwrite)
declare token @llvm.taskframe.create() #1

; Function Attrs: nounwind willreturn memory(argmem: readwrite)
declare void @llvm.taskframe.use(token) #1

attributes #0 = { noinline nounwind optnone uwtable "frame-pointer"="all" "min-legal-vector-width"="0" "no-trapping-math"="true" "stack-protector-buffer-size"="8" "target-cpu"="x86-64" "target-features"="+cx8,+fxsr,+mmx,+sse,+sse2,+x87" "tune-cpu"="generic" }
attributes #1 = { nounwind willreturn memory(argmem: readwrite) }

!llvm.module.flags = !{!0, !1, !2, !3, !4}
!llvm.ident = !{!5}

!0 = !{i32 1, !"wchar_size", i32 4}
!1 = !{i32 8, !"PIC Level", i32 2}
!2 = !{i32 7, !"PIE Level", i32 2}
!3 = !{i32 7, !"uwtable", i32 2}
!4 = !{i32 7, !"frame-pointer", i32 2}
!5 = !{!"clang version 16.0.6 (git@github.com:OpenCilk/opencilk-project.git 3f1a18993b8087b1b06c7e891dceec0c86016197)"}